/*
 * Unit tests for the foundation modules: resolve, listing, utf8,
 * fs_mem, fs_posix, fs_rooted. Assert-based; exit code 0 = pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "openps3ftp/openps3ftp.h"
#include "../src/opftp.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define CHECK_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        failures++; \
    } \
} while (0)

static void test_resolve(void)
{
    char out[1024];
    bool tsl;

    /* absolute */
    CHECK(opftp_path_resolve("/", "/", "/a/b/../c", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/a/c");

    /* relative against cwd */
    CHECK(opftp_path_resolve("/", "/x", "y", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/x/y");

    /* dot and empty */
    CHECK(opftp_path_resolve("/", "/x", ".", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/x");
    CHECK(opftp_path_resolve("/", "/x", "", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/x");

    /* dotdot clamp at root */
    CHECK(opftp_path_resolve("/", "/", "..", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/");
    CHECK(opftp_path_resolve("/", "/a/b", "../../..", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/");

    /* dotdot pops within cwd */
    CHECK(opftp_path_resolve("/", "/a/b", "../c", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/a/c");

    /* non-root root prefix */
    CHECK(opftp_path_resolve("/srv", "/srv/a", "b", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/srv/a/b");
    CHECK(opftp_path_resolve("/srv", "/srv/a", "..", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/srv");
    CHECK(opftp_path_resolve("/srv", "/srv/a", "../..", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/srv");          /* clamp at root */

    /* trailing slash flag */
    CHECK(opftp_path_resolve("/", "/", "a/", out, sizeof(out), &tsl) == 0);
    CHECK(tsl == true);
    CHECK(opftp_path_resolve("/", "/", "a", out, sizeof(out), &tsl) == 0);
    CHECK(tsl == false);

    /* collapse slashes */
    CHECK(opftp_path_resolve("/", "/", "//a//b", out, sizeof(out), NULL) == 0);
    CHECK_STR(out, "/a/b");

    /* parent */
    CHECK(opftp_path_parent("/", out, sizeof(out)) == 0);
    CHECK_STR(out, "");
    CHECK(opftp_path_parent("/a", out, sizeof(out)) == 0);
    CHECK_STR(out, "/");
    CHECK(opftp_path_parent("/a/b", out, sizeof(out)) == 0);
    CHECK_STR(out, "/a");
}

static void test_utf8(void)
{
    CHECK(opftp_utf8_valid("plain"));
    CHECK(opftp_utf8_valid("h\xc3\xa9llo"));            /* héllo */
    CHECK(!opftp_utf8_valid("\xff"));                    /* invalid byte */
    CHECK(!opftp_utf8_valid("\xc3"));                    /* truncated */
    CHECK(!opftp_utf8_valid("\xc0\xaf"));                /* overlong '/' */
    CHECK(!opftp_utf8_valid("\xed\xa0\x80"));            /* surrogate */
    CHECK(!opftp_utf8_valid("\xf4\x90\x80\x80"));        /* > U+10FFFF */
    CHECK(opftp_utf8_valid("\xf0\x9f\x98\x80"));         /* emoji */
}

static void test_listing(void)
{
    char m[11];
    opftp_mode_str(S_IFDIR | 0755, m);
    CHECK_STR(m, "drwxr-xr-x");
    opftp_mode_str(S_IFREG | 0644, m);
    CHECK_STR(m, "-rw-r--r--");

    opftp_dirent_t de = {0};
    snprintf(de.name, sizeof(de.name), "file.txt");
    de.mode = S_IFREG | 0644;
    de.size = 42;
    de.mtime = 1700000000;

    char line[512];
    int n = opftp_listing_format(line, sizeof(line), &de, NULL);
    CHECK(n > 0);
    /* uid/gid 0 -> "1 1", name present, CRLF terminator */
    CHECK(strstr(line, "-rw-r--r-- 1 1 1") != NULL);
    CHECK(strstr(line, "42") != NULL);
    CHECK(strstr(line, "file.txt\r\n") != NULL);
}

static void test_fs_mem(void)
{
    const opftp_fs_t* fs = opftp_fs_mem_create();
    CHECK(fs != NULL);
    if (!fs) return;

    /* mkdir + file create/write/read */
    CHECK(fs->mkdir(fs->ctx, "/docs", 0755) == 0);
    CHECK(fs->mkdir(fs->ctx, "/docs", 0755) != 0);      /* EEXIST */

    int fd;
    CHECK(fs->open(fs->ctx, "/docs/hello.txt", OPFTP_O_WRONLY | OPFTP_O_CREAT | OPFTP_O_TRUNC, 0644, &fd) == 0);
    const char* msg = "hello world";
    CHECK(fs->write(fs->ctx, fd, msg, strlen(msg)) == (ssize_t) strlen(msg));
    CHECK(fs->close(fs->ctx, fd) == 0);

    /* stat */
    opftp_stat_t st;
    CHECK(fs->stat(fs->ctx, "/docs/hello.txt", &st) == 0);
    CHECK(st.size == strlen(msg));
    CHECK((st.mode & S_IFMT) == S_IFREG);

    /* read back */
    CHECK(fs->open(fs->ctx, "/docs/hello.txt", OPFTP_O_RDONLY, 0, &fd) == 0);
    char buf[64] = {0};
    CHECK(fs->read(fs->ctx, fd, buf, sizeof(buf)) == (ssize_t) strlen(msg));
    CHECK_STR(buf, msg);
    CHECK(fs->read(fs->ctx, fd, buf, sizeof(buf)) == 0);  /* EOF */
    CHECK(fs->close(fs->ctx, fd) == 0);

    /* seek + append */
    CHECK(fs->open(fs->ctx, "/docs/hello.txt", OPFTP_O_WRONLY | OPFTP_O_APPEND, 0, &fd) == 0);
    CHECK(fs->write(fs->ctx, fd, "!", 1) == 1);
    CHECK(fs->close(fs->ctx, fd) == 0);
    CHECK(fs->open(fs->ctx, "/docs/hello.txt", OPFTP_O_RDONLY, 0, &fd) == 0);
    CHECK(fs->seek(fs->ctx, fd, 6, SEEK_SET) == 6);
    memset(buf, 0, sizeof(buf));
    CHECK(fs->read(fs->ctx, fd, buf, 6) == 6);
    CHECK_STR(buf, "world!");
    CHECK(fs->close(fs->ctx, fd) == 0);

    /* readdir */
    void* dir;
    CHECK(fs->opendir(fs->ctx, "/docs", &dir) == 0);
    opftp_dirent_t de;
    int found = 0;
    while (fs->readdir(fs->ctx, dir, &de) == 1)
        if (strcmp(de.name, "hello.txt") == 0) found++;
    CHECK(found == 1);
    CHECK(fs->closedir(fs->ctx, dir) == 0);

    /* rename, chmod, unlink, rmdir */
    CHECK(fs->rename(fs->ctx, "/docs/hello.txt", "/docs/renamed.txt") == 0);
    CHECK(fs->stat(fs->ctx, "/docs/hello.txt", &st) != 0);   /* gone */
    CHECK(fs->chmod(fs->ctx, "/docs/renamed.txt", 0600) == 0);
    CHECK(fs->stat(fs->ctx, "/docs/renamed.txt", &st) == 0);
    CHECK((st.mode & 0777) == 0600);
    CHECK(fs->unlink(fs->ctx, "/docs/renamed.txt") == 0);
    CHECK(fs->rmdir(fs->ctx, "/docs") == 0);

    /* nonexistent path */
    CHECK(fs->stat(fs->ctx, "/nope", &st) != 0);
    CHECK(errno == ENOENT);

    opftp_fs_mem_destroy(fs);
}

static void test_fs_posix_and_root(void)
{
    /* sandbox under /tmp */
    char tmpl[] = "/tmp/opftp_test_XXXXXX";
    char* sandbox = mkdtemp(tmpl);
    CHECK(sandbox != NULL);
    if (!sandbox) return;

    char rootpath[1024];
    snprintf(rootpath, sizeof(rootpath), "%s/root", sandbox);
    mkdir(rootpath, 0755);

    const opftp_fs_t* wr = opftp_fs_rooted(&opftp_fs_posix, rootpath);
    CHECK(wr != NULL);

    /* create + read via rooted posix */
    char fpath[1100];
    snprintf(fpath, sizeof(fpath), "%s/data.txt", rootpath);
    int fd;
    CHECK(wr->open(wr->ctx, fpath, OPFTP_O_WRONLY | OPFTP_O_CREAT | OPFTP_O_TRUNC, 0644, &fd) == 0);
    CHECK(wr->write(wr->ctx, fd, "xyz", 3) == 3);
    CHECK(wr->close(wr->ctx, fd) == 0);

    opftp_stat_t st;
    CHECK(wr->stat(wr->ctx, fpath, &st) == 0);
    CHECK(st.size == 3);

    /* escape attempts rejected */
    int fd2;
    CHECK(wr->open(wr->ctx, "/etc/passwd", OPFTP_O_RDONLY, 0, &fd2) != 0);
    CHECK(errno == EACCES);
    CHECK(wr->stat(wr->ctx, "/etc/passwd", &st) != 0);

    /* symlink escape rejected by realpath containment */
    char linkpath[1100];
    snprintf(linkpath, sizeof(linkpath), "%s/evil", rootpath);
    CHECK(symlink("/etc/passwd", linkpath) == 0);
    CHECK(wr->open(wr->ctx, linkpath, OPFTP_O_RDONLY, 0, &fd2) != 0);

    /* parent not existing -> creat target rejected by realpath check */
    char deep[1100];
    snprintf(deep, sizeof(deep), "%s/missing/x.txt", rootpath);
    CHECK(wr->open(wr->ctx, deep, OPFTP_O_WRONLY | OPFTP_O_CREAT, 0644, &fd2) != 0);

    opftp_fs_rooted_free(wr);

    /* cleanup */
    unlink(linkpath);
    unlink(fpath);
    rmdir(rootpath);
    rmdir(sandbox);
}

int main(void)
{
    test_resolve();
    test_utf8();
    test_listing();
    test_fs_mem();
    test_fs_posix_and_root();

    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all unit tests passed\n");
    return 0;
}

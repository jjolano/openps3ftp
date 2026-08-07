/*
 * Host POSIX filesystem backend. Const singleton, no root state —
 * containment is applied by the rooted wrapper.
 */
#include "opftp.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static int posix_open(void* ctx, const char* path, int flags, uint16_t mode, int* fd)
{
    (void) ctx;
    int oflags = 0;
    int acc = flags & 3;                 /* OPFTP_O_RDONLY/WRONLY/RDWR == POSIX */
    if (acc == OPFTP_O_RDONLY) oflags |= O_RDONLY;
    else if (acc == OPFTP_O_WRONLY) oflags |= O_WRONLY;
    else oflags |= O_RDWR;
    if (flags & OPFTP_O_CREAT)  oflags |= O_CREAT;
    if (flags & OPFTP_O_TRUNC)  oflags |= O_TRUNC;
    if (flags & OPFTP_O_APPEND) oflags |= O_APPEND;

    int r = open(path, oflags, mode ? mode : 0644);
    if (r < 0) return -1;
    *fd = r;
    return 0;
}

static int posix_close(void* ctx, int fd)       { (void) ctx; return close(fd); }
static ssize_t posix_read(void* ctx, int fd, void* b, size_t n)
{ (void) ctx; return read(fd, b, n); }
static ssize_t posix_write(void* ctx, int fd, const void* b, size_t n)
{ (void) ctx; return write(fd, b, n); }
static int64_t posix_seek(void* ctx, int fd, int64_t off, int whence)
{ (void) ctx; return (int64_t) lseek(fd, (off_t) off, whence); }

static void stat_to_opftp(const struct stat* st, opftp_stat_t* o)
{
    o->mode = (uint16_t) (st->st_mode & (S_IFMT | 07777));
    o->size = (uint64_t) st->st_size;
    o->mtime = (int64_t) st->st_mtime;
    o->uid = st->st_uid;
    o->gid = st->st_gid;
}

static int posix_fstat(void* ctx, int fd, opftp_stat_t* st)
{
    (void) ctx;
    struct stat s;
    if (fstat(fd, &s) != 0) return -1;
    stat_to_opftp(&s, st);
    return 0;
}

static int posix_stat(void* ctx, const char* path, opftp_stat_t* st)
{
    (void) ctx;
    struct stat s;
    if (stat(path, &s) != 0) return -1;
    stat_to_opftp(&s, st);
    return 0;
}

static int posix_opendir(void* ctx, const char* path, void** dir)
{
    (void) ctx;
    DIR* d = opendir(path);
    if (!d) return -1;
    *dir = d;
    return 0;
}

static int posix_readdir(void* ctx, void* dir, opftp_dirent_t* de)
{
    (void) ctx;
    DIR* d = dir;
    struct dirent* e;
    errno = 0;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        break;
    }
    if (!e) return errno ? -1 : 0;

    snprintf(de->name, sizeof(de->name), "%s", e->d_name);
    de->size = 0;
    de->mtime = 0;
    de->uid = de->gid = 0;

    /* Always stat for metadata (LIST sizes, MLSD facts); the d_type
     * fast path would leave size/mtime at 0 for DT_REG/DIR. */
    int dfd = dirfd(d);
    if (dfd >= 0) {
        struct stat s;
        if (fstatat(dfd, e->d_name, &s, 0) == 0) {
            de->mode = (uint16_t) (s.st_mode & (S_IFMT | 07777));
            de->size = (uint64_t) s.st_size;
            de->mtime = (int64_t) s.st_mtime;
            de->uid = s.st_uid;
            de->gid = s.st_gid;
            return 1;
        }
    }
    unsigned char dt = e->d_type;
    if (dt == DT_DIR) de->mode = S_IFDIR | 0755;
    else de->mode = S_IFREG | 0644;
    return 1;
}

static int posix_closedir(void* ctx, void* dir) { (void) ctx; return closedir(dir); }

static int posix_mkdir(void* ctx, const char* path, uint16_t mode)
{ (void) ctx; return mkdir(path, mode ? mode : 0755); }

static int posix_rmdir(void* ctx, const char* path)   { (void) ctx; return rmdir(path); }
static int posix_unlink(void* ctx, const char* path)  { (void) ctx; return unlink(path); }
static int posix_rename(void* ctx, const char* a, const char* b)
{ (void) ctx; return rename(a, b); }

static int posix_chmod(void* ctx, const char* path, uint16_t mode)
{ (void) ctx; return chmod(path, mode); }

static int posix_utimes(void* ctx, const char* path, int64_t mtime)
{
    (void) ctx;
    struct timespec times[2];
    times[0].tv_sec = 0; times[0].tv_nsec = UTIME_NOW;
    times[1].tv_sec = (time_t) mtime; times[1].tv_nsec = 0;
    if (utimensat(AT_FDCWD, path, times, 0) != 0)
        return -1;
    return 0;
}

const opftp_fs_t opftp_fs_posix = {
    .ctx = NULL,
    .open = posix_open,
    .close = posix_close,
    .read = posix_read,
    .write = posix_write,
    .seek = posix_seek,
    .fstat = posix_fstat,
    .stat = posix_stat,
    .opendir = posix_opendir,
    .readdir = posix_readdir,
    .closedir = posix_closedir,
    .mkdir = posix_mkdir,
    .rmdir = posix_rmdir,
    .unlink = posix_unlink,
    .rename = posix_rename,
    .chmod = posix_chmod,
    .utimes = posix_utimes,
};

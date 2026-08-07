/*
 * Legacy ftpio_* shim — linux backend.
 *
 * The old OpenPS3FTP ftpio.c LINUX branch stuffed FILE and DIR pointer
 * values into int32 fds, which truncates on 64-bit hosts. This rewrite
 * keeps the legacy API surface (int32 fds, ftpstat/ftpdirent) but uses
 * real POSIX file descriptors, mirroring the ps3 branch (sysFs int fds).
 *
 * ftpdirent == struct dirent, ftpstat == struct stat on LINUX
 * (types.h), so no field conversion is needed.
 */
#include "io.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* Path prefix: the legacy server resolves absolute paths against "/".
 * On the PS3 that is the device filesystem; on a host, map "/" onto
 * OPFTP_LEGACY_ROOT (default "/") so the shim behaves like the PS3
 * backend (rooted at the FTP root). */
static char shim_root[4096] = "/";
static bool shim_root_init;

static void shim_full(char* out, size_t outsz, const char* path)
{
    if (!shim_root_init) {
        const char* r = getenv("OPFTP_LEGACY_ROOT");
        if (r && r[0] == '/') {
            snprintf(shim_root, sizeof(shim_root), "%s", r);
            size_t n = strlen(shim_root);
            while (n > 1 && shim_root[n - 1] == '/')
                shim_root[--n] = '\0';
        }
        shim_root_init = true;
    }
    if (shim_root[1] == '\0')
        snprintf(out, outsz, "%s", path);          /* root is "/" */
    else
        snprintf(out, outsz, "%s%s", shim_root, path);
}

/* DIR* handle map: opendir returns a handle; readdir/closedir resolve
 * it back to DIR*. Handles are small ints (no pointer truncation). */
#define SHIM_DIRS 64

struct shim_dir {
    DIR* d;
    bool used;
};

static struct shim_dir shim_dirs[SHIM_DIRS];
static int shim_dir_next;

int32_t ftpio_open(const char* path, int oflags, int32_t* fd)
{
    char full[4200];
    shim_full(full, sizeof(full), path);
    int f = open(full, oflags, 0777);
    if (f < 0)
        return -1;
    *fd = f;
    return 0;
}

int32_t ftpio_opendir(const char* path, int32_t* fd)
{
    char full[4200];
    shim_full(full, sizeof(full), path);
    DIR* d = opendir(full);
    if (!d)
        return -1;
    for (int i = 0; i < SHIM_DIRS; i++) {
        int h = (shim_dir_next + i) % SHIM_DIRS;
        if (!shim_dirs[h].used) {
            shim_dirs[h].d = d;
            shim_dirs[h].used = true;
            shim_dir_next = (h + 1) % SHIM_DIRS;
            *fd = h;
            return 0;
        }
    }
    closedir(d);
    errno = EMFILE;
    return -1;
}

int32_t ftpio_readdir(int32_t fd, ftpdirent* dirent, uint64_t* nread)
{
    if (fd < 0 || fd >= SHIM_DIRS || !shim_dirs[fd].used) {
        errno = EBADF;
        return -1;
    }
    errno = 0;
    ftpdirent* e = readdir(shim_dirs[fd].d);
    if (e != NULL) {
        memcpy(dirent, e, sizeof(ftpdirent));
        *nread = 1;
        return 0;
    }
    if (errno == 0) {
        *nread = 0;
        return 0;
    }
    return -1;
}

int32_t ftpio_read(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nread)
{
    ssize_t n = read(fd, buf, (size_t) nbytes);
    if (n < 0)
        return -1;
    *nread = (uint64_t) n;
    return 0;
}

int32_t ftpio_write(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nwrite)
{
    ssize_t n = write(fd, buf, (size_t) nbytes);
    if (n < 0)
        return -1;
    *nwrite = (uint64_t) n;
    return 0;
}

int32_t ftpio_close(int32_t fd)
{
    return close(fd);
}

int32_t ftpio_closedir(int32_t fd)
{
    if (fd < 0 || fd >= SHIM_DIRS || !shim_dirs[fd].used) {
        errno = EBADF;
        return -1;
    }
    DIR* d = shim_dirs[fd].d;
    shim_dirs[fd].used = false;
    return closedir(d);
}

int32_t ftpio_rename(const char* old_path, const char* new_path)
{
    char a[4200], b[4200];
    shim_full(a, sizeof(a), old_path);
    shim_full(b, sizeof(b), new_path);
    return rename(a, b);
}

int32_t ftpio_chmod(const char* path, mode_t mode)
{
    char full[4200];
    shim_full(full, sizeof(full), path);
    return chmod(full, mode);
}

int32_t ftpio_lseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos)
{
    off_t p = lseek(fd, (off_t) offset, whence);
    if (p == (off_t) -1)
        return -1;
    *pos = (uint64_t) p;
    return 0;
}

int32_t ftpio_mkdir(const char* path, mode_t mode)
{
    char full[4200];
    shim_full(full, sizeof(full), path);
    return mkdir(full, mode);
}

int32_t ftpio_rmdir(const char* path)
{
    char full[4200];
    shim_full(full, sizeof(full), path);
    return rmdir(full);
}

int32_t ftpio_unlink(const char* path)
{
    char full[4200];
    shim_full(full, sizeof(full), path);
    return unlink(full);
}

int32_t ftpio_stat(const char* path, ftpstat* st)
{
    char full[4200];
    shim_full(full, sizeof(full), path);
    return stat(full, st);
}

int32_t ftpio_fstat(int32_t fd, ftpstat* st)
{
    return fstat(fd, st);
}

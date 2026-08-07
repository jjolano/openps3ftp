/*
 * Per-server rooted filesystem wrapper.
 *
 * Wraps a base backend with a root prefix. All paths arriving here are
 * canonical absolute paths from the core resolver; the wrapper:
 *   1. rejects anything not under root (textual prefix check), and
 *   2. for the POSIX base, additionally realpath-checks existing
 *      targets against root (dev-only symlink containment; the TOCTOU
 *      window is accepted and documented — PS3 has no symlinks).
 */
#include "opftp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

struct rooted {
    const opftp_fs_t* base;
    char root[OPFTP_MAX_PATH];
    bool check_realpath;
};

static bool under_root(const struct rooted* r, const char* path)
{
    size_t rl = strlen(r->root);
    if (strncmp(path, r->root, rl) != 0)
        return false;
    if (rl == 1)                    /* root "/" */
        return path[0] == '/';
    return path[rl] == '\0' || path[rl] == '/';
}

/* realpath-based containment: true if the resolved path stays under
 * root. Falls back to checking the nearest existing ancestor when the
 * target does not exist yet (STOR/MKD target, or a missing path — a
 * non-existent path cannot be a symlink escape, and preserving ENOENT
 * lets callers distinguish "not found" from "forbidden"). */
static bool realpath_contained(const struct rooted* r, const char* path)
{
    char resolved[OPFTP_MAX_PATH];
    if (realpath(path, resolved) != NULL)
        return under_root(r, resolved);

    if (errno == ENOENT) {
        char probe[OPFTP_MAX_PATH];
        snprintf(probe, sizeof(probe), "%s", path);
        for (;;) {
            char parent[OPFTP_MAX_PATH];
            if (opftp_path_parent(probe, parent, sizeof(parent)) != 0)
                return false;
            if (parent[0] == '\0')
                return under_root(r, path);   /* nothing exists up to root */
            if (realpath(parent, resolved) != NULL)
                return under_root(r, resolved);
            if (errno != ENOENT)
                return false;
            snprintf(probe, sizeof(probe), "%s", parent);
        }
    }
    return false;
}

static bool check(const struct rooted* r, const char* path)
{
    if (!under_root(r, path)) {
        errno = EACCES;
        return false;
    }
    if (r->check_realpath && !realpath_contained(r, path)) {
        /* a real symlink escape is EACCES; a merely missing target
         * keeps ENOENT so callers can report "No such file..." */
        if (errno != ENOENT)
            errno = EACCES;
        return false;
    }
    return true;
}

static int r_open(void* ctx, const char* path, int flags, uint16_t mode, int* fd)
{
    struct rooted* r = ctx;
    if (!check(r, path)) return -1;
    return r->base->open(r->base->ctx, path, flags, mode, fd);
}
static int r_close(void* ctx, int fd)
{ return ((struct rooted*) ctx)->base->close(((struct rooted*) ctx)->base->ctx, fd); }
static ssize_t r_read(void* ctx, int fd, void* b, size_t n)
{ return ((struct rooted*) ctx)->base->read(((struct rooted*) ctx)->base->ctx, fd, b, n); }
static ssize_t r_write(void* ctx, int fd, const void* b, size_t n)
{ return ((struct rooted*) ctx)->base->write(((struct rooted*) ctx)->base->ctx, fd, b, n); }
static int64_t r_seek(void* ctx, int fd, int64_t off, int whence)
{ return ((struct rooted*) ctx)->base->seek(((struct rooted*) ctx)->base->ctx, fd, off, whence); }
static int r_fstat(void* ctx, int fd, opftp_stat_t* st)
{ return ((struct rooted*) ctx)->base->fstat(((struct rooted*) ctx)->base->ctx, fd, st); }

static int r_stat(void* ctx, const char* path, opftp_stat_t* st)
{
    struct rooted* r = ctx;
    if (!check(r, path)) return -1;
    return r->base->stat(r->base->ctx, path, st);
}

static int r_opendir(void* ctx, const char* path, void** dir)
{
    struct rooted* r = ctx;
    if (!check(r, path)) return -1;
    return r->base->opendir(r->base->ctx, path, dir);
}

static int r_readdir(void* ctx, void* dir, opftp_dirent_t* de)
{ return ((struct rooted*) ctx)->base->readdir(((struct rooted*) ctx)->base->ctx, dir, de); }
static int r_closedir(void* ctx, void* dir)
{ return ((struct rooted*) ctx)->base->closedir(((struct rooted*) ctx)->base->ctx, dir); }

static int r_mkdir(void* ctx, const char* path, uint16_t mode)
{
    struct rooted* r = ctx;
    if (!check(r, path)) return -1;
    return r->base->mkdir(r->base->ctx, path, mode);
}

static int r_rmdir(void* ctx, const char* path)
{
    struct rooted* r = ctx;
    if (!check(r, path)) return -1;
    return r->base->rmdir(r->base->ctx, path);
}

static int r_unlink(void* ctx, const char* path)
{
    struct rooted* r = ctx;
    if (!check(r, path)) return -1;
    return r->base->unlink(r->base->ctx, path);
}

static int r_rename(void* ctx, const char* a, const char* b)
{
    struct rooted* r = ctx;
    if (!check(r, a) || !check(r, b)) { errno = EACCES; return -1; }
    return r->base->rename(r->base->ctx, a, b);
}

static int r_chmod(void* ctx, const char* path, uint16_t mode)
{
    struct rooted* r = ctx;
    if (!check(r, path)) return -1;
    return r->base->chmod(r->base->ctx, path, mode);
}

static int r_utimes(void* ctx, const char* path, int64_t mtime)
{
    struct rooted* r = ctx;
    if (!check(r, path)) return -1;
    return r->base->utimes(r->base->ctx, path, mtime);
}

const opftp_fs_t* opftp_fs_rooted(const opftp_fs_t* base, const char* root)
{
    if (!base || !root || root[0] != '/')
        return NULL;

    struct rooted* r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->base = base;
    snprintf(r->root, sizeof(r->root), "%s", root);
    /* collapse trailing slash so prefix checks are clean */
    size_t rl = strlen(r->root);
    while (rl > 1 && r->root[rl - 1] == '/')
        r->root[--rl] = '\0';
#ifdef OPFTP_PS3
    r->check_realpath = false;   /* ps3 has no symlinks to escape via */
#else
    r->check_realpath = (base == &opftp_fs_posix);
#endif

    opftp_fs_t* fs = calloc(1, sizeof(*fs));
    if (!fs) { free(r); return NULL; }
    fs->ctx = r;
    fs->open = r_open;
    fs->close = r_close;
    fs->read = r_read;
    fs->write = r_write;
    fs->seek = r_seek;
    fs->fstat = r_fstat;
    fs->stat = r_stat;
    fs->opendir = r_opendir;
    fs->readdir = r_readdir;
    fs->closedir = r_closedir;
    fs->mkdir = r_mkdir;
    fs->rmdir = r_rmdir;
    fs->unlink = r_unlink;
    fs->rename = r_rename;
    fs->chmod = r_chmod;
    fs->utimes = r_utimes;
    return fs;
}

void opftp_fs_rooted_free(const opftp_fs_t* fs)
{
    if (!fs) return;
    free(fs->ctx);
    free((void*) fs);
}

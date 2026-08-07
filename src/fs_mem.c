/*
 * In-memory filesystem backend for unit tests. Thread-safe (single
 * mutex). Paths are canonical absolute strings, as the core produces.
 */
#include "opftp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

struct mnode {
    char name[256];
    uint16_t mode;
    uint64_t size;
    int64_t mtime;
    uint32_t uid, gid;
    uint8_t* data;
    struct mnode* children;   /* linked via next */
    struct mnode* parent;
    struct mnode* next;
};

struct mctx {
    struct mnode* root;
    void* mutex;
    struct mfd {
        struct mnode* node;
        uint64_t pos;
        int flags;
        bool open;
    } fds[64];
};

static struct mnode* node_create(const char* name, uint16_t mode)
{
    struct mnode* n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    snprintf(n->name, sizeof(n->name), "%s", name);
    n->mode = mode;
    n->mtime = (int64_t) time(NULL);
    return n;
}

/* Split a canonical absolute path into components. Each comp carries
 * an explicit length (components are not NUL-terminated in place). */
struct comp { const char* p; size_t len; };

static unsigned split_path(const char* path, struct comp comps[256])
{
    unsigned n = 0;
    const char* p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        comps[n].p = p;
        while (*p && *p != '/') p++;
        comps[n].len = (size_t)(p - comps[n].p);
        n++;
    }
    return n;
}

/* Look up a node by path. Returns NULL + errno if not found / not a dir. */
static struct mnode* node_lookup(struct mctx* c, const char* path, int* err)
{
    if (strcmp(path, "/") == 0) return c->root;
    struct comp comps[256];
    unsigned n = split_path(path, comps);
    struct mnode* cur = c->root;
    for (unsigned i = 0; i < n; i++) {
        struct mnode* child = cur->children;
        struct mnode* found = NULL;
        while (child) {
            if (strlen(child->name) == comps[i].len &&
                memcmp(child->name, comps[i].p, comps[i].len) == 0) { found = child; break; }
            child = child->next;
        }
        if (!found) { *err = ENOENT; return NULL; }
        cur = found;
    }
    return cur;
}

static struct mnode* node_lookup_parent(struct mctx* c, const char* path,
                                        struct mnode** parent_out, const char** name_out,
                                        int* err)
{
    if (strcmp(path, "/") == 0) { *err = EEXIST; return NULL; }
    struct comp comps[256];
    unsigned n = split_path(path, comps);
    if (n == 0) { *err = ENOENT; return NULL; }
    struct mnode* cur = c->root;
    for (unsigned i = 0; i + 1 < n; i++) {
        struct mnode* child = cur->children;
        struct mnode* found = NULL;
        while (child) {
            if (strlen(child->name) == comps[i].len &&
                memcmp(child->name, comps[i].p, comps[i].len) == 0) { found = child; break; }
            child = child->next;
        }
        if (!found) { *err = ENOENT; return NULL; }
        cur = found;
        if ((cur->mode & S_IFMT) != S_IFDIR) { *err = ENOTDIR; return NULL; }
    }
    *parent_out = cur;
    *name_out = comps[n - 1].p;   /* last component is NUL-terminated (path tail) */
    return cur;
}

static struct mnode* node_find_child(struct mnode* parent, const char* name)
{
    size_t len = strlen(name);
    for (struct mnode* ch = parent->children; ch; ch = ch->next)
        if (strlen(ch->name) == len && memcmp(ch->name, name, len) == 0)
            return ch;
    return NULL;
}

static int mem_open(void* ctx, const char* path, int flags, uint16_t mode, int* fd)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    int err = 0;
    struct mnode* n = node_lookup(c, path, &err);
    int acc = flags & 3;

    if (!n && (flags & OPFTP_O_CREAT)) {
        struct mnode* parent;
        const char* name;
        if (!node_lookup_parent(c, path, &parent, &name, &err)) {
            opftp_mutex_unlock(c->mutex);
            errno = err;
            return -1;
        }
        if ((parent->mode & S_IFMT) != S_IFDIR) {
            opftp_mutex_unlock(c->mutex);
            errno = ENOTDIR;
            return -1;
        }
        n = node_create(name, S_IFREG | (mode ? mode : 0644));
        if (!n) { opftp_mutex_unlock(c->mutex); errno = ENOMEM; return -1; }
        n->parent = parent;
        n->next = parent->children;
        parent->children = n;
    }
    if (!n) { opftp_mutex_unlock(c->mutex); errno = err; return -1; }
    if ((n->mode & S_IFMT) == S_IFDIR) { opftp_mutex_unlock(c->mutex); errno = EISDIR; return -1; }

    if (flags & OPFTP_O_TRUNC) {
        free(n->data);
        n->data = NULL;
        n->size = 0;
    }

    int slot = -1;
    for (int i = 1; i < 64; i++) {
        if (!c->fds[i].open) { slot = i; break; }
    }
    if (slot < 0) { opftp_mutex_unlock(c->mutex); errno = EMFILE; return -1; }

    c->fds[slot].node = n;
    c->fds[slot].pos = (flags & OPFTP_O_APPEND) ? n->size : 0;
    c->fds[slot].flags = acc;
    c->fds[slot].open = true;
    opftp_mutex_unlock(c->mutex);
    *fd = slot;
    return 0;
}

static int mem_close(void* ctx, int fd)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    if (fd <= 0 || fd >= 64 || !c->fds[fd].open) {
        opftp_mutex_unlock(c->mutex);
        errno = EBADF;
        return -1;
    }
    c->fds[fd].open = false;
    c->fds[fd].node = NULL;
    opftp_mutex_unlock(c->mutex);
    return 0;
}

static ssize_t mem_read(void* ctx, int fd, void* buf, size_t n)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    if (fd <= 0 || fd >= 64 || !c->fds[fd].open || (c->fds[fd].flags & 3) == OPFTP_O_WRONLY) {
        opftp_mutex_unlock(c->mutex);
        errno = EBADF;
        return -1;
    }
    struct mfd* f = &c->fds[fd];
    uint64_t avail = f->node->size > f->pos ? f->node->size - f->pos : 0;
    size_t take = n < avail ? n : (size_t) avail;
    if (take > 0 && f->node->data)
        memcpy(buf, f->node->data + f->pos, take);
    f->pos += take;
    opftp_mutex_unlock(c->mutex);
    return (ssize_t) take;
}

static ssize_t mem_write(void* ctx, int fd, const void* buf, size_t n)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    if (fd <= 0 || fd >= 64 || !c->fds[fd].open || (c->fds[fd].flags & 3) == OPFTP_O_RDONLY) {
        opftp_mutex_unlock(c->mutex);
        errno = EBADF;
        return -1;
    }
    struct mfd* f = &c->fds[fd];
    uint64_t need = f->pos + n;
    if (need > f->node->size) {
        uint8_t* nd = realloc(f->node->data, (size_t) need);
        if (!nd) { opftp_mutex_unlock(c->mutex); errno = ENOMEM; return -1; }
        f->node->data = nd;
        memset(nd + f->node->size, 0, (size_t) (need - f->node->size));
        f->node->size = need;
    }
    memcpy(f->node->data + f->pos, buf, n);
    f->pos += n;
    f->node->mtime = (int64_t) time(NULL);
    opftp_mutex_unlock(c->mutex);
    return (ssize_t) n;
}

static int64_t mem_seek(void* ctx, int fd, int64_t off, int whence)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    if (fd <= 0 || fd >= 64 || !c->fds[fd].open) {
        opftp_mutex_unlock(c->mutex);
        errno = EBADF;
        return -1;
    }
    struct mfd* f = &c->fds[fd];
    int64_t base = 0;
    if (whence == SEEK_CUR) base = (int64_t) f->pos;
    else if (whence == SEEK_END) base = (int64_t) f->node->size;
    else if (whence != SEEK_SET) { opftp_mutex_unlock(c->mutex); errno = EINVAL; return -1; }
    if (base + off < 0) { opftp_mutex_unlock(c->mutex); errno = EINVAL; return -1; }
    f->pos = (uint64_t) (base + off);
    opftp_mutex_unlock(c->mutex);
    return (int64_t) f->pos;
}

static int node_to_stat(struct mnode* n, opftp_stat_t* st)
{
    st->mode = n->mode;
    st->size = n->size;
    st->mtime = n->mtime;
    st->uid = n->uid;
    st->gid = n->gid;
    return 0;
}

static int mem_fstat(void* ctx, int fd, opftp_stat_t* st)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    if (fd <= 0 || fd >= 64 || !c->fds[fd].open) {
        opftp_mutex_unlock(c->mutex);
        errno = EBADF;
        return -1;
    }
    node_to_stat(c->fds[fd].node, st);
    opftp_mutex_unlock(c->mutex);
    return 0;
}

static int mem_stat(void* ctx, const char* path, opftp_stat_t* st)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    int err = 0;
    struct mnode* n = node_lookup(c, path, &err);
    if (!n) { opftp_mutex_unlock(c->mutex); errno = err; return -1; }
    node_to_stat(n, st);
    opftp_mutex_unlock(c->mutex);
    return 0;
}

struct mdir {
    struct mnode* next;
};

static int mem_opendir(void* ctx, const char* path, void** dir)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    int err = 0;
    struct mnode* n = node_lookup(c, path, &err);
    if (!n) { opftp_mutex_unlock(c->mutex); errno = err; return -1; }
    if ((n->mode & S_IFMT) != S_IFDIR) { opftp_mutex_unlock(c->mutex); errno = ENOTDIR; return -1; }
    struct mdir* d = malloc(sizeof(*d));
    if (!d) { opftp_mutex_unlock(c->mutex); errno = ENOMEM; return -1; }
    d->next = n->children;
    opftp_mutex_unlock(c->mutex);
    *dir = d;
    return 0;
}

static int mem_readdir(void* ctx, void* dir, opftp_dirent_t* de)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    struct mdir* d = dir;
    if (!d->next) { opftp_mutex_unlock(c->mutex); return 0; }
    struct mnode* n = d->next;
    d->next = n->next;
    snprintf(de->name, sizeof(de->name), "%s", n->name);
    de->mode = n->mode;
    de->size = n->size;
    de->mtime = n->mtime;
    de->uid = n->uid;
    de->gid = n->gid;
    opftp_mutex_unlock(c->mutex);
    return 1;
}

static int mem_closedir(void* ctx, void* dir)
{
    (void) ctx;
    free(dir);
    return 0;
}

static int mem_mkdir(void* ctx, const char* path, uint16_t mode)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    int err = 0;
    struct mnode* parent;
    const char* name;
    if (!node_lookup_parent(c, path, &parent, &name, &err)) {
        opftp_mutex_unlock(c->mutex);
        errno = err;
        return -1;
    }
    if (node_find_child(parent, name)) { opftp_mutex_unlock(c->mutex); errno = EEXIST; return -1; }
    struct mnode* n = node_create(name, S_IFDIR | (mode ? mode : 0755));
    if (!n) { opftp_mutex_unlock(c->mutex); errno = ENOMEM; return -1; }
    n->parent = parent;
    n->next = parent->children;
    parent->children = n;
    opftp_mutex_unlock(c->mutex);
    return 0;
}

static int mem_rmdir(void* ctx, const char* path)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    int err = 0;
    struct mnode* n = node_lookup(c, path, &err);
    if (!n) { opftp_mutex_unlock(c->mutex); errno = err; return -1; }
    if ((n->mode & S_IFMT) != S_IFDIR) { opftp_mutex_unlock(c->mutex); errno = ENOTDIR; return -1; }
    if (n->children) { opftp_mutex_unlock(c->mutex); errno = ENOTEMPTY; return -1; }
    /* unlink from parent */
    if (n->parent) {
        struct mnode** p = &n->parent->children;
        while (*p && *p != n) p = &(*p)->next;
        if (*p) *p = n->next;
    }
    free(n);
    opftp_mutex_unlock(c->mutex);
    return 0;
}

static int mem_unlink(void* ctx, const char* path)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    int err = 0;
    struct mnode* n = node_lookup(c, path, &err);
    if (!n) { opftp_mutex_unlock(c->mutex); errno = err; return -1; }
    if ((n->mode & S_IFMT) == S_IFDIR) { opftp_mutex_unlock(c->mutex); errno = EISDIR; return -1; }
    if (n->parent) {
        struct mnode** p = &n->parent->children;
        while (*p && *p != n) p = &(*p)->next;
        if (*p) *p = n->next;
    }
    free(n->data);
    free(n);
    opftp_mutex_unlock(c->mutex);
    return 0;
}

static int mem_rename(void* ctx, const char* oldp, const char* newp)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    int err = 0;
    struct mnode* n = node_lookup(c, oldp, &err);
    if (!n) { opftp_mutex_unlock(c->mutex); errno = err; return -1; }
    struct mnode* np;
    const char* nn;
    if (!node_lookup_parent(c, newp, &np, &nn, &err)) {
        opftp_mutex_unlock(c->mutex);
        errno = err;
        return -1;
    }
    struct mnode* existing = node_find_child(np, nn);
    if (existing) {
        if (existing == n) { opftp_mutex_unlock(c->mutex); return 0; }
        if ((existing->mode & S_IFMT) == S_IFDIR || (n->mode & S_IFMT) == S_IFDIR) {
            opftp_mutex_unlock(c->mutex);
            errno = EISDIR;
            return -1;
        }
        /* replace existing file */
        struct mnode** p = &existing->parent->children;
        while (*p && *p != existing) p = &(*p)->next;
        if (*p) *p = existing->next;
        free(existing->data);
        free(existing);
    }
    /* unlink from old parent */
    struct mnode** p = &n->parent->children;
    while (*p && *p != n) p = &(*p)->next;
    if (*p) *p = n->next;
    snprintf(n->name, sizeof(n->name), "%s", nn);
    n->parent = np;
    n->next = np->children;
    np->children = n;
    opftp_mutex_unlock(c->mutex);
    return 0;
}

static int mem_chmod(void* ctx, const char* path, uint16_t mode)
{
    struct mctx* c = ctx;
    opftp_mutex_lock(c->mutex);
    int err = 0;
    struct mnode* n = node_lookup(c, path, &err);
    if (!n) { opftp_mutex_unlock(c->mutex); errno = err; return -1; }
    n->mode = (uint16_t) ((n->mode & S_IFMT) | (mode & 07777));
    opftp_mutex_unlock(c->mutex);
    return 0;
}

static const opftp_fs_t* mem_vtable(void)
{
    static const opftp_fs_t fs = {
        .open = mem_open,
        .close = mem_close,
        .read = mem_read,
        .write = mem_write,
        .seek = mem_seek,
        .fstat = mem_fstat,
        .stat = mem_stat,
        .opendir = mem_opendir,
        .readdir = mem_readdir,
        .closedir = mem_closedir,
        .mkdir = mem_mkdir,
        .rmdir = mem_rmdir,
        .unlink = mem_unlink,
        .rename = mem_rename,
        .chmod = mem_chmod,
    };
    return &fs;
}

/* Test-only constructor: returns an opftp_fs_t with a fresh ctx. */
const opftp_fs_t* opftp_fs_mem_create(void)
{
    struct mctx* c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->root = node_create("/", S_IFDIR | 0755);
    c->mutex = opftp_mutex_create();
    if (!c->root || !c->mutex) {
        free(c->root);
        if (c->mutex) opftp_mutex_destroy(c->mutex);
        free(c);
        return NULL;
    }
    const opftp_fs_t* fs = mem_vtable();
    /* ctx is const in the vtable; return a copy with our ctx */
    opftp_fs_t* fscopy = malloc(sizeof(*fscopy));
    if (!fscopy) { free(c->root); opftp_mutex_destroy(c->mutex); free(c); return NULL; }
    *fscopy = *fs;
    fscopy->ctx = c;
    return fscopy;
}

void opftp_fs_mem_destroy(const opftp_fs_t* fs)
{
    if (!fs) return;
    struct mctx* c = fs->ctx;
    if (c) {
        /* free all nodes */
        struct mnode* stack[512];
        unsigned n = 0;
        stack[n++] = c->root;
        while (n > 0) {
            struct mnode* node = stack[--n];
            for (struct mnode* ch = node->children; ch;) {
                struct mnode* next = ch->next;
                if (n < 512) stack[n++] = ch;
                else free(ch->data), free(ch);
                ch = next;
            }
            free(node->data);
            free(node);
        }
        opftp_mutex_destroy(c->mutex);
        free(c);
    }
    free((void*) fs);
}

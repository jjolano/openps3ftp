/*
 * PS3 filesystem backend (ps3dk).
 * Implements the opftp_fs_t vtable over the cellFs* API (libfs.sprx).
 * Errors: returns -1 + errno; cellFs returns plain errno values (this
 * SDK's cell_fs_file_api.h), which are passed through directly.
 */
#ifdef OPFTP_PS3

#include "opftp.h"
#include <cell/fs/cell_fs_file_api.h>
#include <sys/memory.h>      /* sysMemContainerCreate */
#include <sys/file.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Shared kernel io-buffer pool. Without an io buffer, each cellFsRead/Write
 * does small page reads with per-call syscall overhead; with a 64KB
 * page buffer the kernel batches transfers (the classic PS3 FTP trick).
 * One container for the whole process (cellFsSetDefaultContainer), set up
 * once in opftp_fs_ps3() at boot — before any worker threads or
 * opens exist, so no locking is needed. Non-fatal: if the container can't
 * be allocated, I/O stays unbuffered and still works. */
#define PS3_IO_BUFFER_SIZE  (64 * 1024)
#define PS3_IO_POOL_SIZE    (1024 * 1024)

static void ps3_io_init(void)
{
    sys_mem_container_t cid;
    if (sysMemContainerCreate(&cid, PS3_IO_POOL_SIZE) != 0)
        return;
    cellFsSetDefaultContainer(cid, PS3_IO_POOL_SIZE);
}

/* cellFs error -> errno. This SDK's cellFs returns plain errno values
 * (positive small ints), passed through below. The 0x8001xxxx table is
 * kept defensively in case a runtime libfs.sprx returns raw CELL codes
 * instead. The switch key is the raw u32: for a negative s32 the two's
 * complement form IS the CELL code (abs() would corrupt it). */
static int cell_to_errno(s32 ret)
{
    u32 e = (u32) ret;
    if (ret < 0) {
        s32 m = -ret;
        if (m > 0 && m < 4096)
            return (int) m;              /* plain errno, negative form */
    }
    if (e > 0 && e < 4096)
        return (int) e;                  /* plain errno, positive form */
    switch (e) {
    case 0x80010001: return EPERM;      /* CELL_EPERM */
    case 0x80010005: return EIO;        /* CELL_EIO */
    case 0x80010006: return ENOENT;     /* CELL_ENOENT */
    case 0x80010009: return EBADF;      /* CELL_EBADF */
    case 0x8001000A: return EBUSY;      /* CELL_EBUSY */
    case 0x8001000C: return ENOMEM;     /* CELL_ENOMEM */
    case 0x8001000D: return EACCES;     /* CELL_EACCES */
    case 0x8001000E: return EFAULT;     /* CELL_EFAULT */
    case 0x80010011: return EEXIST;     /* CELL_EEXIST */
    case 0x80010014: return ENOTDIR;    /* CELL_ENOTDIR */
    case 0x80010015: return EISDIR;     /* CELL_EISDIR */
    case 0x80010016: return EINVAL;     /* CELL_EINVAL */
    case 0x80010018: return EMFILE;     /* CELL_EMFILE */
    case 0x8001001C: return ENOSPC;     /* CELL_ENOSPC */
    case 0x8001001E: return EROFS;      /* CELL_EROFS */
    case 0x8001001F: return EFBIG;      /* CELL_EFBIG */
    case 0x80010020: return ESPIPE;     /* CELL_ESPIPE */
    case 0x80010021: return EPIPE;      /* CELL_EPIPE */
    case 0x80010022: return ENAMETOOLONG; /* CELL_ENAMETOOLONG */
    case 0x80010027: return ENOTEMPTY;  /* CELL_ENOTEMPTY */
    case 0x8001002B: return ENOSYS;     /* CELL_ENOSYS */
    default: return EIO;
    }
}

static int ps3_open(void* ctx, const char* path, int flags, uint16_t mode, int* fd)
{
    (void) ctx;
    s32 oflags = 0;
    int acc = flags & 3;
    if (acc == OPFTP_O_WRONLY) oflags = CELL_FS_O_WRONLY;
    else if (acc == OPFTP_O_RDWR) oflags = CELL_FS_O_RDWR;
    else oflags = CELL_FS_O_RDONLY;
    if (flags & OPFTP_O_CREAT)  oflags |= CELL_FS_O_CREAT;
    if (flags & OPFTP_O_TRUNC)  oflags |= CELL_FS_O_TRUNC;
    if (flags & OPFTP_O_APPEND) oflags |= CELL_FS_O_APPEND;

    int f = -1;
    s32 rc = cellFsOpen(path, oflags, &f, NULL, 0);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    /* best-effort: give the fd a kernel io buffer from the shared pool
     * (set up in opftp_fs_ps3(); failure just means unbuffered I/O) */
    cellFsSetIoBufferFromDefaultContainer(f, PS3_IO_BUFFER_SIZE,
                                         CELL_FS_IO_BUFFER_PAGE_SIZE_64KB);
    *fd = f;
    return 0;
}

static int ps3_close(void* ctx, int fd) { (void) ctx; return cellFsClose(fd); }

static ssize_t ps3_read(void* ctx, int fd, void* buf, size_t n)
{
    (void) ctx;
    u64 got = 0;
    if (cellFsRead(fd, buf, n, &got) != 0) { errno = EIO; return -1; }
    return (ssize_t) got;
}

static ssize_t ps3_write(void* ctx, int fd, const void* buf, size_t n)
{
    (void) ctx;
    u64 wrote = 0;
    if (cellFsWrite(fd, buf, n, &wrote) != 0) { errno = EIO; return -1; }
    return (ssize_t) wrote;
}

static int64_t ps3_seek(void* ctx, int fd, int64_t off, int whence)
{
    (void) ctx;
    u64 pos = 0;
    s32 rc = cellFsLseek(fd, off, whence, &pos);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    return (int64_t) pos;
}

static void fsstat_to_opftp(const CellFsStat* st, opftp_stat_t* o)
{
    o->mode = (uint16_t) st->st_mode;
    o->size = st->st_size;
    o->mtime = (int64_t) st->st_mtime;
    o->uid = 0;
    o->gid = 0;
}

static int ps3_fstat(void* ctx, int fd, opftp_stat_t* st)
{
    (void) ctx;
    CellFsStat s;
    if (cellFsFstat(fd, &s) != 0) { errno = EIO; return -1; }
    fsstat_to_opftp(&s, st);
    return 0;
}

static int ps3_stat(void* ctx, const char* path, opftp_stat_t* st)
{
    (void) ctx;
    CellFsStat s;
    s32 rc = cellFsStat(path, &s);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    fsstat_to_opftp(&s, st);
    return 0;
}

/* Directory handle: fd + a batch of entries. cellFsGetDirectoryEntries
 * returns N entries (with full stat metadata) per syscall, so listing a
 * large dir costs ~1/32 the syscalls of cellFsReaddir AND avoids a
 * per-entry stat — the dominant "small IO" cost for LIST/NLST. */
#define PS3_DIR_BATCH 32

struct ps3_dir {
    int fd;
    CellFsDirectoryEntry batch[PS3_DIR_BATCH];
    u32 idx, count;
};

static int ps3_opendir(void* ctx, const char* path, void** dir)
{
    (void) ctx;
    int fd = -1;
    s32 rc = cellFsOpendir(path, &fd);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    struct ps3_dir* d = calloc(1, sizeof(*d));
    if (!d) {
        cellFsClosedir(fd);
        errno = ENOMEM;
        return -1;
    }
    d->fd = fd;
    *dir = d;
    return 0;
}

static int ps3_readdir(void* ctx, void* dir, opftp_dirent_t* de)
{
    (void) ctx;
    struct ps3_dir* d = dir;
    const CellFsDirectoryEntry* e;
    /* cellFsGetDirectoryEntries yields "." and ".."; the vtable contract
     * says backends don't (the host backend filters them too, and a
     * recursive walk would otherwise descend into "." forever). */
    for (;;) {
        if (d->idx >= d->count) {
            u32 got = 0;
            s32 rc = cellFsGetDirectoryEntries(d->fd, d->batch,
                                              sizeof(CellFsDirectoryEntry), &got);
            if (rc != 0) { errno = cell_to_errno(rc); return -1; }
            if (got == 0)
                return 0;                 /* EOF */
            d->count = got;
            d->idx = 0;
        }
        e = &d->batch[d->idx++];
        const char* n = e->entry_name.d_name;
        if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')))
            continue;
        break;
    }
    snprintf(de->name, sizeof(de->name), "%s", e->entry_name.d_name);
    de->mode = (uint16_t) e->attribute.st_mode;   /* full S_IFMT|perm bits */
    de->size = e->attribute.st_size;
    de->mtime = (int64_t) e->attribute.st_mtime;
    de->uid = (uint32_t) e->attribute.st_uid;
    de->gid = (uint32_t) e->attribute.st_gid;
    return 1;
}

static int ps3_closedir(void* ctx, void* dir)
{
    (void) ctx;
    struct ps3_dir* d = dir;
    s32 rc = cellFsClosedir(d->fd);
    free(d);
    return rc;
}

static int ps3_mkdir(void* ctx, const char* path, uint16_t mode)
{
    (void) ctx;
    s32 rc = cellFsMkdir(path, mode);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    return 0;
}

static int ps3_rmdir(void* ctx, const char* path)
{
    (void) ctx;
    s32 rc = cellFsRmdir(path);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    return 0;
}

static int ps3_unlink(void* ctx, const char* path)
{
    (void) ctx;
    s32 rc = cellFsUnlink(path);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    return 0;
}

static int ps3_rename(void* ctx, const char* a, const char* b)
{
    (void) ctx;
    s32 rc = cellFsRename(a, b);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    return 0;
}

static int ps3_chmod(void* ctx, const char* path, uint16_t mode)
{
    (void) ctx;
    s32 rc = cellFsChmod(path, mode);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    return 0;
}

static int ps3_utimes(void* ctx, const char* path, int64_t mtime)
{
    (void) ctx;
    CellFsUtimbuf t;
    t.actime = time(NULL);
    t.modtime = (time_t) mtime;
    s32 rc = cellFsUtime(path, &t);
    if (rc != 0) { errno = cell_to_errno(rc); return -1; }
    return 0;
}

const opftp_fs_t* opftp_fs_ps3(void)
{
    static int io_pool_ready = 0;
    if (!io_pool_ready) {
        ps3_io_init();   /* one shared io-buffer pool for all fds */
        io_pool_ready = 1;
    }
    static const opftp_fs_t fs = {
        .ctx = NULL,
        .open = ps3_open,
        .close = ps3_close,
        .read = ps3_read,
        .write = ps3_write,
        .seek = ps3_seek,
        .fstat = ps3_fstat,
        .stat = ps3_stat,
        .opendir = ps3_opendir,
        .readdir = ps3_readdir,
        .closedir = ps3_closedir,
        .mkdir = ps3_mkdir,
        .rmdir = ps3_rmdir,
        .unlink = ps3_unlink,
        .rename = ps3_rename,
        .chmod = ps3_chmod,
        .utimes = ps3_utimes,
    };
    return &fs;
}

#endif /* OPFTP_PS3 */

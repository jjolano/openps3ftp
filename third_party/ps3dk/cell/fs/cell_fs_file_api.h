/*
 * cell_fs_file_api.h — file-system surface for the sys_fs SPRX.
 *
 * The reference SDK splits these declarations across <sys/fs.h>
 * (re-includes <sys/fs_external.h>) and <cell/fs/cell_fs_file_api.h>;
 * we flatten the type definitions into this single header so that
 * <cell/cell_fs.h> is a one-stop include.
 *
 * Stub linkage: link `-lfs_stub` (built from
 * tools/nidgen/nids/cellFs.yaml).  The 49 functions declared here
 * cover the published 475-era surface; 10 additional symbols (allocate
 * file area variants, sdata-with-version, arcade hdd serial, l10n
 * conversion callbacks) are exported by the SPRX but were stripped
 * from the public 475 headers — they live in the stub archive only;
 * apps that need them forward-declare locally.
 */
#ifndef PS3TC_CELL_FS_CELL_FS_FILE_API_H
#define PS3TC_CELL_FS_CELL_FS_FILE_API_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sys_memory_container_t — user-supplied memory container handle.  Same
 * 32-bit kernel handle PSL1GHT calls sys_mem_container_t and the
 * reference SDK aliases as sys_memory_container_t.  Forward-declared
 * here so this header is independent of <sys/memory.h>. */
typedef uint32_t sys_memory_container_t;

/* ---- Path / name length limits ---- */
#define CELL_FS_MAX_FS_PATH_LENGTH       (1024)
#define CELL_FS_MAX_FS_FILE_NAME_LENGTH  (255)
#define CELL_FS_MAX_MP_LENGTH            (31)

/* ---- File mode bits ---- */
typedef int CellFsMode;

#define CELL_FS_S_IFMT   0170000
#define CELL_FS_S_IFDIR  0040000
#define CELL_FS_S_IFREG  0100000
#define CELL_FS_S_IFLNK  0120000
#define CELL_FS_S_IFWHT  0160000

#define CELL_FS_S_IRWXU  0000700
#define CELL_FS_S_IRUSR  0000400
#define CELL_FS_S_IWUSR  0000200
#define CELL_FS_S_IXUSR  0000100
#define CELL_FS_S_IRWXG  0000070
#define CELL_FS_S_IRGRP  0000040
#define CELL_FS_S_IWGRP  0000020
#define CELL_FS_S_IXGRP  0000010
#define CELL_FS_S_IRWXO  0000007
#define CELL_FS_S_IROTH  0000004
#define CELL_FS_S_IWOTH  0000002
#define CELL_FS_S_IXOTH  0000001

#define CELL_FS_OTH_CREATE_MODE_RW       (CELL_FS_S_IRWXO & ~CELL_FS_S_IXOTH)
#define CELL_FS_OTH_CREATE_MODE_RX       (CELL_FS_S_IRWXO & ~CELL_FS_S_IWOTH)
#define CELL_FS_DEFAULT_CREATE_MODE_1    CELL_FS_S_IRWXU
#define CELL_FS_DEFAULT_CREATE_MODE_2    (CELL_FS_S_IRUSR | CELL_FS_S_IWUSR)
#define CELL_FS_DEFAULT_CREATE_MODE_3    (CELL_FS_S_IRUSR | CELL_FS_S_IXUSR)
#define CELL_FS_DEFAULT_CREATE_MODE_4    CELL_FS_S_IRUSR
#define CELL_FS_DEFAULT_CREATE_MODE_5    (CELL_FS_S_IRWXU | CELL_FS_S_IRWXO)
#define CELL_FS_DEFAULT_CREATE_MODE_6 \
    (CELL_FS_DEFAULT_CREATE_MODE_2 | CELL_FS_OTH_CREATE_MODE_RW)
#define CELL_FS_DEFAULT_CREATE_MODE_7 \
    (CELL_FS_DEFAULT_CREATE_MODE_3 | CELL_FS_OTH_CREATE_MODE_RX)
#define CELL_FS_DEFAULT_CREATE_MODE_8    (CELL_FS_S_IRUSR | CELL_FS_S_IROTH)

/* ---- Dirent type (CellFsDirent.d_type) ---- */
#define CELL_FS_TYPE_UNKNOWN    0
#define CELL_FS_TYPE_DIRECTORY  1
#define CELL_FS_TYPE_REGULAR    2
#define CELL_FS_TYPE_SYMLINK    3

#define NO_UID      (-1)
#define SYSTEM_UID  (0)
#define NO_GID      (-1)

/* ---- Open flags (cellFsOpen) ---- */
#define CELL_FS_O_CREAT     000100
#define CELL_FS_O_EXCL      000200
#define CELL_FS_O_TRUNC     001000
#define CELL_FS_O_APPEND    002000
#define CELL_FS_O_ACCMODE   000003
#define CELL_FS_O_RDONLY    000000
#define CELL_FS_O_RDWR      000002
#define CELL_FS_O_WRONLY    000001
#define CELL_FS_O_MSELF     010000

/* ---- Seek whence (cellFsLseek) ---- */
#ifndef SEEK_SET
#  define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#  define SEEK_CUR 1
#endif
#ifndef SEEK_END
#  define SEEK_END 2
#endif
#define CELL_FS_SEEK_SET  SEEK_SET
#define CELL_FS_SEEK_CUR  SEEK_CUR
#define CELL_FS_SEEK_END  SEEK_END

/* ---- IO buffer page sizes (cellFsSetIoBuffer) ---- */
#define CELL_FS_IO_BUFFER_PAGE_SIZE_64KB  0x0002
#define CELL_FS_IO_BUFFER_PAGE_SIZE_1MB   0x0004

/* ---- Streaming-read constants (CellFsRingBuffer.copy + St status) ---- */
#define CELL_FS_ST_COPY              0
#define CELL_FS_ST_COPYLESS          1
#define CELL_FS_ST_INITIALIZED       0x0001
#define CELL_FS_ST_NOT_INITIALIZED   0x0002
#define CELL_FS_ST_STOP              0x0100
#define CELL_FS_ST_PROGRESS          0x0200

/* ---- Async-IO limits (cellFsAioInit) ---- */
#define CELL_FS_AIO_MAX_FS       10
#define CELL_FS_AIO_MAX_REQUEST  32

/* ---- PS3-specific errno values (not in newlib's <errno.h>).
 *
 * Guarded so apps that declare these via private kernel headers see
 * no redefinition.  Numeric values follow the publicly-documented
 * lv2-kernel layout. */
#ifndef EFPOS
#  define EFPOS         0x32
#endif
#ifndef EFSSPECIFIC
#  define EFSSPECIFIC   0x88
#endif
#ifndef ENOTMOUNTED
#  define ENOTMOUNTED   0x89
#endif
#ifndef ENOTMSELF
#  define ENOTMSELF     0x8A
#endif
#ifndef ENOTSDATA
#  define ENOTSDATA     0x8B
#endif
#ifndef EAUTHFATAL
#  define EAUTHFATAL    0x8C
#endif

/* ---- Errno surface ---- */
typedef int CellFsErrno;

#ifndef CELL_OK
#  define CELL_OK 0
#endif

#define CELL_FS_OK                 CELL_OK
#define CELL_FS_SUCCEEDED          CELL_FS_OK

#define CELL_FS_ERROR_EDOM         EDOM
#define CELL_FS_ERROR_EFAULT       EFAULT
#define CELL_FS_ERROR_EFBIG        EFBIG
#define CELL_FS_ERROR_EFPOS        EFPOS
#define CELL_FS_ERROR_EMLINK       EMLINK
#define CELL_FS_ERROR_ENFILE       ENFILE
#define CELL_FS_ERROR_ENOENT       ENOENT
#define CELL_FS_ERROR_ENOSPC       ENOSPC
#define CELL_FS_ERROR_ENOTTY       ENOTTY
#define CELL_FS_ERROR_EPIPE        EPIPE
#define CELL_FS_ERROR_ERANGE       ERANGE
#define CELL_FS_ERROR_EROFS        EROFS
#define CELL_FS_ERROR_ESPIPE       ESPIPE
#define CELL_FS_ERROR_E2BIG        E2BIG
#define CELL_FS_ERROR_EACCES       EACCES
#define CELL_FS_ERROR_EAGAIN       EAGAIN
#define CELL_FS_ERROR_EBADF        EBADF
#define CELL_FS_ERROR_EBUSY        EBUSY
#define CELL_FS_ERROR_ECHILD       ECHILD
#define CELL_FS_ERROR_EEXIST       EEXIST
#define CELL_FS_ERROR_EINTR        EINTR
#define CELL_FS_ERROR_EINVAL       EINVAL
#define CELL_FS_ERROR_EIO          EIO
#define CELL_FS_ERROR_EISDIR       EISDIR
#define CELL_FS_ERROR_EMFILE       EMFILE
#define CELL_FS_ERROR_ENODEV       ENODEV
#define CELL_FS_ERROR_ENOEXEC      ENOEXEC
#define CELL_FS_ERROR_ENOMEM       ENOMEM
#define CELL_FS_ERROR_ENOTDIR      ENOTDIR
#define CELL_FS_ERROR_ENXIO        ENXIO
#define CELL_FS_ERROR_EPERM        EPERM
#define CELL_FS_ERROR_ESRCH        ESRCH
#define CELL_FS_ERROR_EXDEV        EXDEV
#define CELL_FS_ERROR_EBADMSG      EBADMSG
#define CELL_FS_ERROR_ECANCELED    ECANCELED
#define CELL_FS_ERROR_EDEADLK      EDEADLK
#define CELL_FS_ERROR_EILSEQ       EILSEQ
#define CELL_FS_ERROR_EINPROGRESS  EINPROGRESS
#define CELL_FS_ERROR_EMSGSIZE     EMSGSIZE
#define CELL_FS_ERROR_ENAMETOOLONG ENAMETOOLONG
#define CELL_FS_ERROR_ENOLCK       ENOLCK
#define CELL_FS_ERROR_ENOSYS       ENOSYS
#define CELL_FS_ERROR_ENOTEMPTY    ENOTEMPTY
#define CELL_FS_ERROR_ENOTSUP      ENOTSUP
#define CELL_FS_ERROR_ETIMEDOUT    ETIMEDOUT
#define CELL_FS_ERROR_EFSSPECIFIC  EFSSPECIFIC
#define CELL_FS_ERROR_EOVERFLOW    EOVERFLOW
#define CELL_FS_ERROR_ENOTMOUNTED  ENOTMOUNTED
#define CELL_FS_ERROR_ENOTMSELF    ENOTMSELF
#define CELL_FS_ERROR_ENOTSDATA    ENOTSDATA
#define CELL_FS_ERROR_EAUTHFATAL   EAUTHFATAL

/* ---- Unprefixed CELL_FS_E* aliases (reference SDK shipped both forms) ---- */
#define CELL_FS_EDOM         CELL_FS_ERROR_EDOM
#define CELL_FS_EFAULT       CELL_FS_ERROR_EFAULT
#define CELL_FS_EFBIG        CELL_FS_ERROR_EFBIG
#define CELL_FS_EFPOS        CELL_FS_ERROR_EFPOS
#define CELL_FS_EMLINK       CELL_FS_ERROR_EMLINK
#define CELL_FS_ENFILE       CELL_FS_ERROR_ENFILE
#define CELL_FS_ENOENT       CELL_FS_ERROR_ENOENT
#define CELL_FS_ENOSPC       CELL_FS_ERROR_ENOSPC
#define CELL_FS_ENOTTY       CELL_FS_ERROR_ENOTTY
#define CELL_FS_EPIPE        CELL_FS_ERROR_EPIPE
#define CELL_FS_ERANGE       CELL_FS_ERROR_ERANGE
#define CELL_FS_EROFS        CELL_FS_ERROR_EROFS
#define CELL_FS_ESPIPE       CELL_FS_ERROR_ESPIPE
#define CELL_FS_E2BIG        CELL_FS_ERROR_E2BIG
#define CELL_FS_EACCES       CELL_FS_ERROR_EACCES
#define CELL_FS_EAGAIN       CELL_FS_ERROR_EAGAIN
#define CELL_FS_EBADF        CELL_FS_ERROR_EBADF
#define CELL_FS_EBUSY        CELL_FS_ERROR_EBUSY
#define CELL_FS_ECHILD       CELL_FS_ERROR_ECHILD
#define CELL_FS_EEXIST       CELL_FS_ERROR_EEXIST
#define CELL_FS_EINTR        CELL_FS_ERROR_EINTR
#define CELL_FS_EINVAL       CELL_FS_ERROR_EINVAL
#define CELL_FS_EIO          CELL_FS_ERROR_EIO
#define CELL_FS_EISDIR       CELL_FS_ERROR_EISDIR
#define CELL_FS_EMFILE       CELL_FS_ERROR_EMFILE
#define CELL_FS_ENODEV       CELL_FS_ERROR_ENODEV
#define CELL_FS_ENOEXEC      CELL_FS_ERROR_ENOEXEC
#define CELL_FS_ENOMEM       CELL_FS_ERROR_ENOMEM
#define CELL_FS_ENOTDIR      CELL_FS_ERROR_ENOTDIR
#define CELL_FS_ENXIO        CELL_FS_ERROR_ENXIO
#define CELL_FS_EPERM        CELL_FS_ERROR_EPERM
#define CELL_FS_ESRCH        CELL_FS_ERROR_ESRCH
#define CELL_FS_EXDEV        CELL_FS_ERROR_EXDEV
#define CELL_FS_EBADMSG      CELL_FS_ERROR_EBADMSG
#define CELL_FS_ECANCELED    CELL_FS_ERROR_ECANCELED
#define CELL_FS_EDEADLK      CELL_FS_ERROR_EDEADLK
#define CELL_FS_EILSEQ       CELL_FS_ERROR_EILSEQ
#define CELL_FS_EINPROGRESS  CELL_FS_ERROR_EINPROGRESS
#define CELL_FS_EMSGSIZE     CELL_FS_ERROR_EMSGSIZE
#define CELL_FS_ENAMETOOLONG CELL_FS_ERROR_ENAMETOOLONG
#define CELL_FS_ENOLCK       CELL_FS_ERROR_ENOLCK
#define CELL_FS_ENOSYS       CELL_FS_ERROR_ENOSYS
#define CELL_FS_ENOTEMPTY    CELL_FS_ERROR_ENOTEMPTY
#define CELL_FS_ENOTSUP      CELL_FS_ERROR_ENOTSUP
#define CELL_FS_ETIMEDOUT    CELL_FS_ERROR_ETIMEDOUT
#define CELL_FS_EFSSPECIFIC  CELL_FS_ERROR_EFSSPECIFIC
#define CELL_FS_EOVERFLOW    CELL_FS_ERROR_EOVERFLOW
#define CELL_FS_ENOTMOUNTED  CELL_FS_ERROR_ENOTMOUNTED
#define CELL_FS_ENOTMSELF    CELL_FS_ERROR_ENOTMSELF
#define CELL_FS_ENOTSDATA    CELL_FS_ERROR_ENOTSDATA
#define CELL_FS_EAUTHFATAL   CELL_FS_ERROR_EAUTHFATAL

/* ---- Types ----
 *
 * pack(4) matches the reference SDK's struct layout.  CellFsStat has
 * 8-byte uint64 + time_t fields whose natural alignment would otherwise
 * insert padding that the SPRX does not expect. */
#pragma pack(push, 4)

typedef struct CellFsDirent
{
    uint8_t d_type;       /* one of CELL_FS_TYPE_* */
    uint8_t d_namlen;
    char    d_name[CELL_FS_MAX_FS_FILE_NAME_LENGTH + 1];
} CellFsDirent;

typedef struct CellFsStat
{
    CellFsMode st_mode;
    int        st_uid;
    int        st_gid;
    time_t     st_atime;
    time_t     st_mtime;
    time_t     st_ctime;
    uint64_t   st_size;
    uint64_t   st_blksize;
} CellFsStat;

typedef struct CellFsUtimbuf
{
    time_t actime;
    time_t modtime;
} CellFsUtimbuf;

typedef struct CellFsDirectoryEntry
{
    CellFsStat   attribute;
    CellFsDirent entry_name;
} CellFsDirectoryEntry;

typedef enum CellFsDiscReadRetryType
{
    CELL_FS_DISC_READ_RETRY_NONE     = 0,
    CELL_FS_DISC_READ_RETRY_DEFAULT  = 1
} CellFsDiscReadRetryType;

typedef struct CellFsRingBuffer
{
    uint64_t ringbuf_size;
    uint64_t block_size;
    uint64_t transfer_rate;
    int      copy;          /* CELL_FS_ST_COPY or CELL_FS_ST_COPYLESS */
} CellFsRingBuffer;

typedef struct CellFsAio
{
    int      fd;
    uint64_t offset;
    void    *buf;
    uint64_t size;
    uint64_t user_data;
} CellFsAio;

#pragma pack(pop)

/* ---- File I/O ---- */
CellFsErrno cellFsOpen(const char *path, int flags, int *fd,
                       const void *arg, uint64_t size);
CellFsErrno cellFsRead(int fd, void *buf, uint64_t nbytes,
                       uint64_t *nread);
CellFsErrno cellFsWrite(int fd, const void *buf, uint64_t nbytes,
                        uint64_t *nwrite);
CellFsErrno cellFsClose(int fd);
CellFsErrno cellFsLseek(int fd, int64_t offset, int whence,
                        uint64_t *pos);
CellFsErrno cellFsFsync(int fd);

/* ---- Directory I/O ---- */
CellFsErrno cellFsOpendir(const char *path, int *fd);
CellFsErrno cellFsReaddir(int fd, CellFsDirent *dir, uint64_t *nread);
CellFsErrno cellFsClosedir(int fd);
CellFsErrno cellFsGetDirectoryEntries(int fd,
                                      CellFsDirectoryEntry *entries,
                                      uint32_t entries_size,
                                      uint32_t *data_count);

/* ---- Stat / metadata ---- */
CellFsErrno cellFsStat(const char *path, CellFsStat *sb);
CellFsErrno cellFsFstat(int fd, CellFsStat *sb);
CellFsErrno cellFsChmod(const char *path, CellFsMode mode);
CellFsErrno cellFsUtime(const char *path, const CellFsUtimbuf *timep);

/* ---- Path operations ---- */
CellFsErrno cellFsMkdir(const char *path, CellFsMode mode);
CellFsErrno cellFsRmdir(const char *path);
CellFsErrno cellFsRename(const char *from, const char *to);
CellFsErrno cellFsUnlink(const char *path);

/* ---- Block / size queries ---- */
CellFsErrno cellFsGetBlockSize(const char *path,
                               uint64_t *sector_size,
                               uint64_t *block_size);
CellFsErrno cellFsFGetBlockSize(int fd, uint64_t *sector_size,
                                uint64_t *block_size);
CellFsErrno cellFsGetFreeSize(const char *directory_path,
                              uint32_t *fs_block_size,
                              uint64_t *free_block_count);

/* ---- Truncation ---- */
CellFsErrno cellFsTruncate(const char *path, uint64_t size);
CellFsErrno cellFsFtruncate(int fd, uint64_t size);

/* ---- File-area allocation (SDK 200+) ---- */
CellFsErrno cellFsAllocateFileAreaWithoutZeroFill(const char *path,
                                                  uint64_t size);

/* ---- IO buffers (SDK 220+) ---- */
CellFsErrno cellFsSetIoBuffer(int fd, size_t buffer_size_limit,
                              int page_type,
                              sys_memory_container_t container);
CellFsErrno cellFsSetDefaultContainer(sys_memory_container_t container,
                                      size_t total_limit);
CellFsErrno cellFsSetIoBufferFromDefaultContainer(int fd,
                                                  size_t buffer_size_limit,
                                                  int page_type);

/* ---- Offset I/O (SDK 240+) ---- */
CellFsErrno cellFsReadWithOffset(int fd, uint64_t offset, void *buffer,
                                 uint64_t buffer_size,
                                 uint64_t *read_bytes);
CellFsErrno cellFsWriteWithOffset(int fd, uint64_t offset,
                                  const void *data, uint64_t data_size,
                                  uint64_t *written_bytes);

/* ---- Sdata (signed-data archive) ---- */
CellFsErrno cellFsSdataOpen(const char *path, int flags, int *fd,
                            const void *arg, uint64_t size);
CellFsErrno cellFsSdataOpenByFd(int mself_fd, int flags, int *sdata_fd,
                                uint64_t offset, const void *arg,
                                uint64_t size);

/* ---- Disc retry policy ---- */
CellFsErrno cellFsSetDiscReadRetrySetting(CellFsDiscReadRetryType retry_type);

/* ---- Streaming read (St) ---- */
CellFsErrno cellFsStReadInit(int fd, const CellFsRingBuffer *ringbuf);
CellFsErrno cellFsStReadFinish(int fd);
CellFsErrno cellFsStReadGetRingBuf(int fd, CellFsRingBuffer *ringbuf);
CellFsErrno cellFsStReadGetStatus(int fd, uint64_t *status);
CellFsErrno cellFsStReadGetRegid(int fd, uint64_t *regid);
CellFsErrno cellFsStReadStart(int fd, uint64_t offset, uint64_t size);
CellFsErrno cellFsStReadStop(int fd);
CellFsErrno cellFsStRead(int fd, char *buf, uint64_t size,
                         uint64_t *rsize);
CellFsErrno cellFsStReadGetCurrentAddr(int fd, char **addr,
                                       uint64_t *size);
CellFsErrno cellFsStReadPutCurrentAddr(int fd, char *addr, uint64_t size);
CellFsErrno cellFsStReadWait(int fd, uint64_t size);
CellFsErrno cellFsStReadWaitCallback(int fd, uint64_t size,
                                     void (*func)(int xfd, uint64_t xsize));

/* ---- Async I/O ---- */
CellFsErrno cellFsAioInit(const char *mount_point);
CellFsErrno cellFsAioFinish(const char *mount_point);
CellFsErrno cellFsAioRead(CellFsAio *aio, int *id,
                          void (*func)(CellFsAio *aio, CellFsErrno err,
                                       int id, uint64_t size));
CellFsErrno cellFsAioWrite(CellFsAio *aio, int *id,
                           void (*func)(CellFsAio *aio, CellFsErrno err,
                                        int id, uint64_t size));
CellFsErrno cellFsAioCancel(int id);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ---- recovered from libfs.sprx firmware disassembly (SDK 4.76-era) ---- */
CellFsErrno cellFsTruncate2(const char *path, uint64_t size);
CellFsErrno cellFsAllocateFileAreaWithInitialData(const char *path, uint64_t size);
CellFsErrno cellFsAllocateFileAreaByFdWithInitialData(int fd, uint64_t size);
CellFsErrno cellFsAllocateFileAreaByFdWithoutZeroFill(int fd, uint64_t size);
CellFsErrno cellFsChangeFileSizeWithoutAllocation(const char *path, uint64_t size);
CellFsErrno cellFsChangeFileSizeByFdWithoutAllocation(int fd, uint64_t size);
CellFsErrno cellFsArcadeHddSerialNumber(uint64_t *serialNumber);
CellFsErrno cellFsRegisterConversionCallback(int type, void *callback, void *userdata);
CellFsErrno cellFsUnregisterL10nCallbacks(int id, void *a, void *b);
CellFsErrno cellFsSdataOpenWithVersion(const char *path, int flags, uint64_t version,
                                       int *fd, void *arg);

#endif /* PS3TC_CELL_FS_CELL_FS_FILE_API_H */

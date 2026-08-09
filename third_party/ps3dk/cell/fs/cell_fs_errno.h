/*
 * cell_fs_errno.h — errno surface forward.
 *
 * Re-includes the file API header (which carries every CELL_FS_ERROR_*
 * + CELL_FS_E* alias).  Reference SDK splits errno + file API, but the
 * errno header itself just forwards to <sys/fs.h>; we flatten that one
 * level so applications see the same macro set whether they include
 * <cell/cell_fs.h> or just <cell/fs/cell_fs_errno.h>.
 */
#ifndef PS3TC_CELL_FS_CELL_FS_ERRNO_H
#define PS3TC_CELL_FS_CELL_FS_ERRNO_H

#include <cell/fs/cell_fs_file_api.h>

#endif /* PS3TC_CELL_FS_CELL_FS_ERRNO_H */

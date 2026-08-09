#include "io.h"

int32_t ftpio_open(const char* path, int oflags, int32_t* fd)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsOpen(path, oflags, fd, NULL, 0);
	#endif

	return ret;
}

int32_t ftpio_opendir(const char* path, int32_t* fd)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsOpendir(path, fd);
	#endif

	return ret;
}

int32_t ftpio_readdir(int32_t fd, ftpdirent* dirent, uint64_t* nread)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsReaddir(fd, dirent, nread);
	#endif

	return ret;
}

int32_t ftpio_read(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nread)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsRead(fd, buf, nbytes, nread);
	#endif

	return ret;
}

int32_t ftpio_write(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nwrite)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsWrite(fd, buf, nbytes, nwrite);
	#endif

	return ret;
}

int32_t ftpio_close(int32_t fd)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsClose(fd);
	#endif

	return ret;
}

int32_t ftpio_closedir(int32_t fd)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsClosedir(fd);
	#endif

	return ret;
}

int32_t ftpio_rename(const char* old_path, const char* new_path)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysLv2FsRename(old_path, new_path);
	#endif

	return ret;
}

int32_t ftpio_chmod(const char* path, mode_t mode)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsChmod(path, mode);
	#endif

	return ret;
}

int32_t ftpio_lseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsLseek(fd, offset, whence, pos);
	#endif

	return ret;
}

int32_t ftpio_mkdir(const char* path, mode_t mode)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsMkdir(path, mode);
	#endif

	return ret;
}

int32_t ftpio_rmdir(const char* path)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsRmdir(path);
	#endif

	return ret;
}

int32_t ftpio_unlink(const char* path)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsUnlink(path);
	#endif

	return ret;
}

int32_t ftpio_stat(const char* path, ftpstat* st)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsStat(path, st);
	#endif

	return ret;
}

int32_t ftpio_fstat(int32_t fd, ftpstat* st)
{
	int32_t ret = -1;

	#ifdef PSL1GHT_SDK
	ret = sysFsFstat(fd, st);
	#endif

	return ret;
}
#include "io.h"

int32_t ftpio_open(const char* path, int oflags, int32_t* fd)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsOpen(path, oflags, fd, NULL, 0);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsOpen(path, oflags, fd, NULL, 0);
	#endif

	#ifdef LINUX
	*fd = open(path, oflags);

	if(*fd != -1)
	{
		ret = 0;
	}
	#endif

	return ret;
}

int32_t ftpio_opendir(const char* path, int32_t* fd)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsOpendir(path, fd);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsOpendir(path, fd);
	#endif

	#ifdef LINUX
	DIR* dirp = opendir(path);

	if(dirp != NULL)
	{
		*fd = ((intptr_t) ((void*) dirp));
		ret = 0;
	}
	#endif

	return ret;
}

int32_t ftpio_readdir(int32_t fd, ftpdirent* dirent, uint64_t* nread)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsReaddir(fd, dirent, nread);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsReaddir(fd, dirent, nread);
	#endif

	#ifdef LINUX
	errno = 0;

	DIR* dirp = (DIR*) ((void*) ((intptr_t) fd));

	ftpdirent* dirent_temp = readdir(dirp);
	
	if(dirent_temp != NULL)
	{
		memcpy(dirent, dirent_temp, sizeof(ftpdirent));
		
		*nread = 1;
		ret = 0;
	}
	else
	{
		if(errno == 0)
		{
			*nread = 0;
			ret = 0;
		}
	}
	#endif

	return ret;
}

int32_t ftpio_read(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nread)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsRead(fd, buf, nbytes, nread);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsRead(fd, buf, nbytes, nread);
	#endif

	#ifdef LINUX
	ssize_t nread_bytes = read(fd, buf, (size_t) nbytes);

	if(nread_bytes > -1)
	{
		*nread = (uint64_t) nread_bytes;
		ret = 0;
	}
	#endif

	return ret;
}

int32_t ftpio_write(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nwrite)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsWrite(fd, buf, nbytes, nwrite);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsWrite(fd, buf, nbytes, nwrite);
	#endif

	#ifdef LINUX
	ssize_t nwrite_bytes = write(fd, buf, (size_t) nbytes);

	if(nwrite_bytes > -1)
	{
		*nwrite = (uint64_t) nwrite_bytes;
		ret = 0;
	}
	#endif

	return ret;
}

int32_t ftpio_close(int32_t fd)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsClose(fd);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsClose(fd);
	#endif

	#ifdef LINUX
	ret = close(fd);
	#endif

	return ret;
}

int32_t ftpio_closedir(int32_t fd)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsClosedir(fd);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsClosedir(fd);
	#endif

	#ifdef LINUX
	ret = closedir((DIR*) ((intptr_t) fd));
	#endif

	return ret;
}

int32_t ftpio_rename(const char* old_path, const char* new_path)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsRename(old_path, new_path);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysLv2FsRename(old_path, new_path);
	#endif

	#ifdef LINUX
	ret = rename(old_path, new_path);
	#endif

	return ret;
}

int32_t ftpio_chmod(const char* path, mode_t mode)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsChmod(path, mode);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsChmod(path, mode);
	#endif

	#ifdef LINUX
	ret = chmod(path, mode);
	#endif

	return ret;
}

int32_t ftpio_lseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsLseek(fd, offset, whence, pos);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsLseek(fd, offset, whence, pos);
	#endif

	#ifdef LINUX
	off_t new_pos = lseek(fd, (off_t) offset, whence);

	if(new_pos != -1)
	{
		*pos = new_pos;
		ret = 0;
	}
	#endif

	return ret;
}

int32_t ftpio_mkdir(const char* path, mode_t mode)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsMkdir(path, mode);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsMkdir(path, mode);
	#endif

	#ifdef LINUX
	ret = mkdir(path, mode);
	#endif

	return ret;
}

int32_t ftpio_rmdir(const char* path)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsRmdir(path);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsRmdir(path);
	#endif

	#ifdef LINUX
	ret = rmdir(path);
	#endif

	return ret;
}

int32_t ftpio_unlink(const char* path)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsUnlink(path);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsUnlink(path);
	#endif

	#ifdef LINUX
	ret = unlink(path);
	#endif

	return ret;
}

int32_t ftpio_stat(const char* path, ftpstat* st)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsStat(path, st);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsStat(path, st);
	#endif

	#ifdef LINUX
	ret = stat(path, st);
	#endif

	return ret;
}

int32_t ftpio_fstat(int32_t fd, ftpstat* st)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	ret = cellFsFstat(fd, st);
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsFstat(fd, st);
	#endif

	#ifdef LINUX
	ret = fstat(fd, st);
	#endif

	return ret;
}

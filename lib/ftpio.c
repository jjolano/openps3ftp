#include "io.h"

int32_t ftpio_open(const char* path, int oflags, int32_t* fd)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if(str_startswith(path, "/dev_ntfs"))
	{
		if(ps3ntfs_running() && ps3ntfs_open != NULL)
		{
			char ntfspath[MAX_PATH];
			get_ntfspath(ntfspath, path);

			*fd = ps3ntfs_open(ntfspath, oflags, 0777);

			if(*fd != -1)
			{
				*fd |= NTFS_FD_MASK;
				ret = 0;
			}

			return ret;
		}
	}
	#endif

	FILE* fp = NULL;

	if(oflags & O_CREAT)
	{
		fp = fopen(path, "wb");
	}
	else
	if(oflags & O_APPEND)
	{
		fp = fopen(path, "ab+");
	}
	else
	if(oflags & O_RDONLY)
	{
		fp = fopen(path, "r");
	}
	else
	if(oflags & O_TRUNC)
	{
		fp = fopen(path, "wb+");
	}
	else
	{
		fp = fopen(path, "rb+");
	}

	if(fp != NULL)
	{
		*fd = (intptr_t) fp;
		ret = 0;
	}

	return ret;
}

int32_t ftpio_opendir(const char* path, int32_t* fd)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if(str_startswith(path, "/dev_ntfs"))
	{
		if(ps3ntfs_running() && ps3ntfs_diropen != NULL)
		{
			char ntfspath[MAX_PATH];
			get_ntfspath(ntfspath, path);

			DIR_ITER* dirState = ps3ntfs_diropen(ntfspath);

			if(dirState != NULL)
			{
				*fd = ((intptr_t) dirState) | NTFS_FD_MASK;
				ret = 0;
			}

			return ret;
		}
	}
	#endif

	DIR* dirp = opendir(path);

	if(dirp != NULL)
	{
		*fd = (intptr_t) dirp;
		ret = 0;
	}

	return ret;
}

int32_t ftpio_readdir(int32_t fd, ftpdirent* dirent, uint64_t* nread)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		if(ps3ntfs_running() && ps3ntfs_dirnext != NULL)
		{
			DIR_ITER* dirState = (DIR_ITER*) (intptr_t) (fd & ~NTFS_FD_MASK);

			struct stat filestat;
			ret = ps3ntfs_dirnext(dirState, dirent->d_name, &filestat);

			if(ret == 0)
			{
				*nread = 1;
			}
			else
			{
				*nread = 0;
				ret = 0;
			}

			return ret;
		}
	}
	#endif

	DIR* dirp = (DIR*) (intptr_t) fd;

	errno = 0;

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

	return ret;
}

int32_t ftpio_read(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nread)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		if(ps3ntfs_running() && ps3ntfs_read != NULL)
		{
			int32_t ntfsfd = (fd & ~NTFS_FD_MASK);
			ssize_t nread_bytes = ps3ntfs_read(ntfsfd, buf, (size_t) nbytes);

			if(nread_bytes > -1)
			{
				*nread = (uint64_t) nread_bytes;
				ret = 0;
			}

			return ret;
		}
	}
	#endif

	FILE* fp = (FILE*) (intptr_t) fd;

	*nread = (uint64_t) fread(buf, sizeof(char), (size_t) nbytes, fp);
	ret = 0;

	return ret;
}

int32_t ftpio_write(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nwrite)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		if(ps3ntfs_running() && ps3ntfs_write != NULL)
		{
			int32_t ntfsfd = (fd & ~NTFS_FD_MASK);
			ssize_t nwrite_bytes = ps3ntfs_write(ntfsfd, buf, (size_t) nbytes);

			if(nwrite_bytes > -1)
			{
				*nwrite = (uint64_t) nwrite_bytes;
				ret = 0;
			}

			return ret;
		}
	}
	#endif

	FILE* fp = (FILE*) (intptr_t) fd;

	*nwrite = (uint64_t) fwrite(buf, sizeof(char), (size_t) nbytes, fp);
	ret = 0;

	return ret;
}

int32_t ftpio_close(int32_t fd)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		if(ps3ntfs_running() && ps3ntfs_close != NULL)
		{
			int32_t ntfsfd = (fd & ~NTFS_FD_MASK);
			ret = ps3ntfs_close(ntfsfd);
		}
		
		return ret;
	}
	#endif

	FILE* fp = (FILE*) (intptr_t) fd;

	ret = fclose(fp);

	return ret;
}

int32_t ftpio_closedir(int32_t fd)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		if(ps3ntfs_running() && ps3ntfs_dirclose != NULL)
		{
			DIR_ITER* dirState = (DIR_ITER*) (intptr_t) (fd & ~NTFS_FD_MASK);
			ret = ps3ntfs_dirclose(dirState);
		}

		return ret;
	}
	#endif

	DIR* dirp = (DIR*) (intptr_t) fd;
	
	ret = closedir(dirp);

	return ret;
}

int32_t ftpio_rename(const char* old_path, const char* new_path)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if(str_startswith(old_path, "/dev_ntfs") || str_startswith(new_path, "/dev_ntfs"))
	{
		if(ps3ntfs_running() && ps3ntfs_rename != NULL)
		{
			char old_ntfspath[MAX_PATH];
			char new_ntfspath[MAX_PATH];

			if(str_startswith(old_path, "/dev_ntfs"))
			{
				get_ntfspath(old_ntfspath, old_path);
			}
			else
			{
				strcpy(old_ntfspath, old_path);
			}

			if(str_startswith(new_path, "/dev_ntfs"))
			{
				get_ntfspath(new_ntfspath, new_path);
			}
			else
			{
				strcpy(new_ntfspath, new_path);
			}

			ret = ps3ntfs_rename(old_ntfspath, new_ntfspath);
		}
		
		return ret;
	}
	#endif
	
	ret = rename(old_path, new_path);

	return ret;
}

int32_t ftpio_chmod(const char* path, mode_t mode)
{
	int32_t ret = -1;

	#ifndef LINUX
	if(str_startswith(path, "/dev_ntfs"))
	{
		#ifdef _NTFS_SUPPORT_
		// not supported
		#endif
	}
	else
	{
		#ifdef CELL_SDK
		ret = cellFsChmod(path, mode);
		#endif

		#ifdef PSL1GHT_SDK
		ret = sysFsChmod(path, mode);
		#endif
	}
	#else
	ret = chmod(path, mode);
	#endif

	return ret;
}

int32_t ftpio_lseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		if(ps3ntfs_running() && ps3ntfs_seek64 != NULL)
		{
			int32_t ntfsfd = (fd & ~NTFS_FD_MASK);
			int64_t new_pos = ps3ntfs_seek64(ntfsfd, offset, whence);

			if(new_pos != -1)
			{
				*pos = new_pos;
				ret = 0;
			}
		}

		return ret;
	}
	#endif

	FILE* fp = (FILE*) (intptr_t) fd;

	off_t new_pos = fseek(fp, (off_t) offset, whence);

	if(new_pos == 0)
	{
		*pos = new_pos;
		ret = 0;
	}

	return ret;
}

int32_t ftpio_mkdir(const char* path, mode_t mode)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if(str_startswith(path, "/dev_ntfs"))
	{
		if(ps3ntfs_running() && ps3ntfs_mkdir != NULL)
		{
			char ntfspath[MAX_PATH];
			get_ntfspath(ntfspath, path);

			ret = ps3ntfs_mkdir(ntfspath, mode);
		}

		return ret;
	}
	#endif

	ret = mkdir(path, mode);

	return ret;
}

int32_t ftpio_rmdir(const char* path)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if(str_startswith(path, "/dev_ntfs"))
	{
		if(ps3ntfs_running() && ps3ntfs_unlink != NULL)
		{
			char ntfspath[MAX_PATH];
			get_ntfspath(ntfspath, path);

			ret = ps3ntfs_unlink(ntfspath);
		}

		return ret;
	}
	#endif
	
	ret = rmdir(path);

	return ret;
}

int32_t ftpio_unlink(const char* path)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if(str_startswith(path, "/dev_ntfs"))
	{
		if(ps3ntfs_running() && ps3ntfs_unlink != NULL)
		{
			char ntfspath[MAX_PATH];
			get_ntfspath(ntfspath, path);

			ret = ps3ntfs_unlink(ntfspath);
		}
		
		return ret;
	}
	#endif
	
	ret = unlink(path);

	return ret;
}

int32_t ftpio_stat(const char* path, ftpstat* st)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if(str_startswith(path, "/dev_ntfs"))
	{
		if(ps3ntfs_running() && ps3ntfs_stat != NULL)
		{
			char ntfspath[MAX_PATH];
			get_ntfspath(ntfspath, path);

			struct stat ntfs_st;
			ret = ps3ntfs_stat(ntfspath, &ntfs_st);

			if(ret == 0)
			{
				memset(st, 0, sizeof(ftpstat));

				st->st_mode = ntfs_st.st_mode;
				st->st_uid = ntfs_st.st_uid;
				st->st_gid = ntfs_st.st_gid;
				st->st_atime = ntfs_st.st_atime;
				st->st_mtime = ntfs_st.st_mtime;
				st->st_ctime = ntfs_st.st_ctime;
				st->st_size = ntfs_st.st_size;
				st->st_blksize = ntfs_st.st_blksize;
			}
		}
		
		return ret;
	}
	#endif

	ret = stat(path, st);

	return ret;
}

int32_t ftpio_fstat(int32_t fd, ftpstat* st)
{
	int32_t ret = -1;

	#ifdef _NTFS_SUPPORT_
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		if(ps3ntfs_running() && ps3ntfs_fstat != NULL)
		{
			int32_t ntfsfd = (fd & ~NTFS_FD_MASK);

			struct stat ntfs_st;
			ret = ps3ntfs_fstat(ntfsfd, &ntfs_st);

			if(ret == 0)
			{
				memset(st, 0, sizeof(ftpstat));

				st->st_mode = ntfs_st.st_mode;
				st->st_uid = ntfs_st.st_uid;
				st->st_gid = ntfs_st.st_gid;
				st->st_atime = ntfs_st.st_atime;
				st->st_mtime = ntfs_st.st_mtime;
				st->st_ctime = ntfs_st.st_ctime;
				st->st_size = ntfs_st.st_size;
				st->st_blksize = ntfs_st.st_blksize;
			}
		}
		
		return ret;
	}
	#endif

	FILE* fp = (FILE*) (intptr_t) fd;
	
	ret = fstat(fileno(fp), st);

	return ret;
}

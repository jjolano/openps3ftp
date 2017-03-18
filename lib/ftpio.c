#include "io.h"

int32_t ftpio_open(const char* path, int oflags, int32_t* fd)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	if(str_startswith(path, "/dev_ntfs"))
	{
		#ifdef _NTFS_SUPPORT_
		char* path_temp = strdup(path);

		char* mount_path = strchr(path_temp + 1, '/');
		char mount_name[16];

		char ntfspath[MAX_PATH];
		
		if(mount_path != NULL)
		{
			strncpy(mount_name, path_temp + 5, mount_path - path_temp);
			sprintf(ntfspath, "%s:%s", mount_name, mount_path);
		}
		else
		{
			strcpy(mount_name, path_temp + 5);
			sprintf(ntfspath, "%s:/", mount_name);
		}

		free(path_temp);

		*fd = ps3ntfs_open(ntfspath, oflags, 0);

		if(*fd != -1)
		{
			*fd |= NTFS_FD_MASK;
			ret = 0;
		}
		#endif
	}
	else
	{
		ret = cellFsOpen(path, oflags, fd, NULL, 0);
	}
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
	if(str_startswith(path, "/dev_ntfs"))
	{
		#ifdef _NTFS_SUPPORT_
		char* path_temp = strdup(path);

		char* mount_path = strchr(path_temp + 1, '/');
		char mount_name[16];

		char ntfspath[MAX_PATH];
		
		if(mount_path != NULL)
		{
			strncpy(mount_name, path_temp + 5, mount_path - path_temp);
			sprintf(ntfspath, "%s:%s", mount_name, mount_path);
		}
		else
		{
			strcpy(mount_name, path_temp + 5);
			sprintf(ntfspath, "%s:/", mount_name);
		}

		free(path_temp);

		DIR_ITER* dirState = ps3ntfs_diropen(ntfspath);

		if(dirState != NULL)
		{
			*fd = ((intptr_t) dirState) | NTFS_FD_MASK;
			ret = 0;
		}
		#endif
	}
	else
	{
		ret = cellFsOpendir(path, fd);
	}
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsOpendir(path, fd);
	#endif

	#ifdef LINUX
	DIR* dirp = opendir(path);

	if(dirp != NULL)
	{
		*fd = (intptr_t) dirp;
		ret = 0;
	}
	#endif

	return ret;
}

int32_t ftpio_readdir(int32_t fd, ftpdirent* dirent, uint64_t* nread)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		#ifdef _NTFS_SUPPORT_
		DIR_ITER* dirState = (DIR_ITER*) (intptr_t) (fd & ~NTFS_FD_MASK);

		struct stat filestat;
		ret = ps3ntfs_dirnext(dirState, dirent->d_name, &filestat);

		if(ret == 0)
		{
			*nread = 1;
		}
		else
		{
			if(ps3ntfs_errno() == 0)
			{
				*nread = 0;
				ret = 0;
			}
		}
		#endif
	}
	else
	{
		ret = cellFsReaddir(fd, dirent, nread);
	}
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsReaddir(fd, dirent, nread);
	#endif

	#ifdef LINUX
	errno = 0;

	DIR* dirp = (DIR*) (intptr_t) fd;

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
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		#ifdef _NTFS_SUPPORT_
		int32_t ntfsfd = (fd & ~NTFS_FD_MASK);
		ssize_t nread_bytes = ps3ntfs_read(ntfsfd, buf, (size_t) nbytes);

		if(nread_bytes > -1)
		{
			*nread = (uint64_t) nread_bytes;
			ret = 0;
		}
		#endif
	}
	else
	{
		ret = cellFsRead(fd, buf, nbytes, nread);
	}
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
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		#ifdef _NTFS_SUPPORT_
		int32_t ntfsfd = (fd & ~NTFS_FD_MASK);
		ssize_t nwrite_bytes = ps3ntfs_write(ntfsfd, buf, (size_t) nbytes);

		if(nwrite_bytes > -1)
		{
			*nwrite = (uint64_t) nwrite_bytes;
			ret = 0;
		}
		#endif
	}
	else
	{
		ret = cellFsWrite(fd, buf, nbytes, nwrite);
	}
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
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		#ifdef _NTFS_SUPPORT_
		int32_t ntfsfd = (fd & ~NTFS_FD_MASK);
		ret = ps3ntfs_close(ntfsfd);
		#endif
	}
	else
	{
		ret = cellFsClose(fd);
	}
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
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		#ifdef _NTFS_SUPPORT_
		DIR_ITER* dirState = (DIR_ITER*) (intptr_t) (fd & ~NTFS_FD_MASK);
		ret = ps3ntfs_dirclose(dirState);
		#endif
	}
	else
	{
		ret = cellFsClosedir(fd);
	}
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsClosedir(fd);
	#endif

	#ifdef LINUX
	ret = closedir((DIR*) (intptr_t) fd);
	#endif

	return ret;
}

int32_t ftpio_rename(const char* old_path, const char* new_path)
{
	int32_t ret = -1;

	#ifdef CELL_SDK
	if(str_startswith(old_path, "/dev_ntfs") || str_startswith(new_path, "/dev_ntfs"))
	{
		#ifdef _NTFS_SUPPORT_
		char old_ntfspath[MAX_PATH];
		char new_ntfspath[MAX_PATH];

		if(str_startswith(old_path, "/dev_ntfs"))
		{
			char* old_path_temp = strdup(old_path);

			char* mount_path = strchr(old_path_temp + 1, '/');
			char mount_name[16];

			if(mount_path != NULL)
			{
				strncpy(mount_name, old_path_temp + 5, mount_path - old_path_temp);
				sprintf(old_ntfspath, "%s:%s", mount_name, mount_path);
			}
			else
			{
				strcpy(mount_name, old_path_temp + 5);
				sprintf(old_ntfspath, "%s:/", mount_name);
			}

			free(old_path_temp);
		}
		else
		{
			strcpy(old_ntfspath, old_path);
		}

		if(str_startswith(new_path, "/dev_ntfs"))
		{
			char* new_path_temp = strdup(new_path);

			char* mount_path = strchr(new_path_temp + 1, '/');
			char mount_name[16];
			
			if(mount_path != NULL)
			{
				strncpy(mount_name, new_path_temp + 5, mount_path - new_path_temp);
				sprintf(new_ntfspath, "%s:%s", mount_name, mount_path);
			}
			else
			{
				strcpy(mount_name, new_path_temp + 5);
				sprintf(new_ntfspath, "%s:/", mount_name);
			}

			free(new_path_temp);
		}
		else
		{
			strcpy(new_ntfspath, new_path);
		}

		ret = ps3ntfs_rename(old_ntfspath, new_ntfspath);
		#endif
	}
	else
	{
		ret = cellFsRename(old_path, new_path);
	}
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
	if(str_startswith(path, "/dev_ntfs"))
	{
		// not supported
	}
	else
	{
		ret = cellFsChmod(path, mode);
	}
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
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		#ifdef _NTFS_SUPPORT_
		int32_t ntfsfd = (fd & ~NTFS_FD_MASK);
		int64_t new_pos = ps3ntfs_seek64(ntfsfd, offset, whence);

		if(new_pos != -1)
		{
			*pos = new_pos;
			ret = 0;
		}
		#endif
	}
	else
	{
		ret = cellFsLseek(fd, offset, whence, pos);
	}
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
	if(str_startswith(path, "/dev_ntfs"))
	{
		#ifdef _NTFS_SUPPORT_
		char* path_temp = strdup(path);

		char* mount_path = strchr(path_temp + 1, '/');
		char mount_name[16];

		char ntfspath[MAX_PATH];
		
		if(mount_path != NULL)
		{
			strncpy(mount_name, path_temp + 5, mount_path - path_temp);
			sprintf(ntfspath, "%s:%s", mount_name, mount_path);
		}
		else
		{
			strcpy(mount_name, path_temp + 5);
			sprintf(ntfspath, "%s:/", mount_name);
		}

		free(path_temp);

		ret = ps3ntfs_mkdir(ntfspath, mode);
		#endif
	}
	else
	{
		ret = cellFsMkdir(path, mode);
	}
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
	if(str_startswith(path, "/dev_ntfs"))
	{
		#ifdef _NTFS_SUPPORT_
		char* path_temp = strdup(path);

		char* mount_path = strchr(path_temp + 1, '/');
		char mount_name[16];

		char ntfspath[MAX_PATH];
		
		if(mount_path != NULL)
		{
			strncpy(mount_name, path_temp + 5, mount_path - path_temp);
			sprintf(ntfspath, "%s:%s", mount_name, mount_path);
		}
		else
		{
			strcpy(mount_name, path_temp + 5);
			sprintf(ntfspath, "%s:/", mount_name);
		}

		free(path_temp);

		ret = ps3ntfs_unlink(ntfspath);
		#endif
	}
	else
	{
		ret = cellFsRmdir(path);
	}
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
	if(str_startswith(path, "/dev_ntfs"))
	{
		#ifdef _NTFS_SUPPORT_
		char* path_temp = strdup(path);

		char* mount_path = strchr(path_temp + 1, '/');
		char mount_name[16];

		char ntfspath[MAX_PATH];
		
		if(mount_path != NULL)
		{
			strncpy(mount_name, path_temp + 5, mount_path - path_temp);
			sprintf(ntfspath, "%s:%s", mount_name, mount_path);
		}
		else
		{
			strcpy(mount_name, path_temp + 5);
			sprintf(ntfspath, "%s:/", mount_name);
		}

		free(path_temp);

		ret = ps3ntfs_unlink(ntfspath);
		#endif
	}
	else
	{
		ret = cellFsUnlink(path);
	}
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
	if(str_startswith(path, "/dev_ntfs"))
	{
		#ifdef _NTFS_SUPPORT_
		char* path_temp = strdup(path);

		char* mount_path = strchr(path_temp + 1, '/');
		char mount_name[16];

		char ntfspath[MAX_PATH];
		
		if(mount_path != NULL)
		{
			strncpy(mount_name, path_temp + 5, mount_path - path_temp);
			sprintf(ntfspath, "%s:%s", mount_name, mount_path);
		}
		else
		{
			strcpy(mount_name, path_temp + 5);
			sprintf(ntfspath, "%s:/", mount_name);
		}

		free(path_temp);

		struct stat ntfs_st;
		ret = ps3ntfs_stat(ntfspath, &ntfs_st);

		if(ret == 0)
		{
			st->st_mode = ntfs_st.st_mode;
			st->st_uid = ntfs_st.st_uid;
			st->st_gid = ntfs_st.st_gid;
			st->st_atime = ntfs_st.st_atime;
			st->st_mtime = ntfs_st.st_mtime;
			st->st_ctime = ntfs_st.st_ctime;
			st->st_size = ntfs_st.st_size;
			st->st_blksize = ntfs_st.st_blksize;
		}
		#endif
	}
	else
	{
		ret = cellFsStat(path, st);
	}
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
	if((fd & NTFS_FD_MASK) == NTFS_FD_MASK)
	{
		#ifdef _NTFS_SUPPORT_
		int32_t ntfsfd = (fd & ~NTFS_FD_MASK);

		struct stat ntfs_st;
		ret = ps3ntfs_fstat(ntfsfd, &ntfs_st);

		if(ret == 0)
		{
			st->st_mode = ntfs_st.st_mode;
			st->st_uid = ntfs_st.st_uid;
			st->st_gid = ntfs_st.st_gid;
			st->st_atime = ntfs_st.st_atime;
			st->st_mtime = ntfs_st.st_mtime;
			st->st_ctime = ntfs_st.st_ctime;
			st->st_size = ntfs_st.st_size;
			st->st_blksize = ntfs_st.st_blksize;
		}
		#endif
	}
	else
	{
		ret = cellFsFstat(fd, st);
	}
	#endif

	#ifdef PSL1GHT_SDK
	ret = sysFsFstat(fd, st);
	#endif

	#ifdef LINUX
	ret = fstat(fd, st);
	#endif

	return ret;
}

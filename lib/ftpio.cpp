/* io.cpp: FTP io class. */

#include "io.hpp"

namespace FTP
{
	namespace IO
	{
		int32_t open(std::string path, int oflags, int32_t* fd)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsOpen(path.c_str(), oflags, fd, NULL, 0);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsOpen(path.c_str(), oflags, fd, NULL, 0);
			#endif

			#ifdef LINUX
			*fd = ::open(path.c_str(), oflags);

			if(*fd != -1)
			{
				ret = 0;
			}
			#endif

			return ret;
		}

		int32_t opendir(std::string path, int32_t* fd)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsOpendir(path.c_str(), fd);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsOpendir(path.c_str(), fd);
			#endif

			#ifdef LINUX
			DIR* dirp = ::opendir(path.c_str());

			if(dirp != NULL)
			{
				*fd = (int32_t) ((intptr_t) dirp);
				ret = 0;
			}
			#endif

			return ret;
		}

		int32_t readdir(int32_t fd, ftpdirent* dirent, uint64_t* nread)
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

			*dirent = ::readdir((DIR*) ((intptr_t) fd));
			
			if(*dirent != NULL)
			{
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

		int32_t read(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nread)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsRead(fd, buf, nbytes, nread);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsRead(fd, buf, nbytes, nread);
			#endif

			#ifdef LINUX
			ssize_t nread_bytes = ::read(fd, buf, (size_t) nbytes);

			if(nread_bytes > -1)
			{
				*nread = (uint64_t) nread_bytes;
				ret = 0;
			}
			#endif

			return ret;
		}

		int32_t write(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nwrite)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsWrite(fd, buf, nbytes, nwrite);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsWrite(fd, buf, nbytes, nwrite);
			#endif

			#ifdef LINUX
			ssize_t nwrite_bytes = ::write(fd, buf, (size_t) nbytes);

			if(nwrite_bytes > -1)
			{
				*nwrite = (uint64_t) nwrite_bytes;
				ret = 0;
			}
			#endif

			return ret;
		}

		int32_t close(int32_t fd)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsClose(fd);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsClose(fd);
			#endif

			#ifdef LINUX
			ret = ::close(fd);
			#endif

			return ret;
		}

		int32_t closedir(int32_t fd)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsClosedir(fd);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsClosedir(fd);
			#endif

			#ifdef LINUX
			ret = ::closedir((DIR*) ((intptr_t) fd));
			#endif

			return ret;
		}

		int32_t rename(std::string old_path, std::string new_path)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsRename(old_path.c_str(), new_path.c_str());
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysLv2FsRename(old_path.c_str(), new_path.c_str());
			#endif

			#ifdef LINUX
			ret = ::rename(old_path.c_str(), new_path.c_str());
			#endif

			return ret;
		}

		int32_t chmod(std::string path, mode_t mode)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsChmod(path.c_str(), mode);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsChmod(path.c_str(), mode);
			#endif

			#ifdef LINUX
			ret = ::chmod(path.c_str(), mode);
			#endif

			return ret;
		}

		int32_t lseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsLseek(fd, offset, whence, pos);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsLseek(fd, offset, whence, pos);
			#endif

			#ifdef LINUX
			off_t new_pos = ::lseek(fd, (off_t) offset, whence);

			if(new_pos != -1)
			{
				*pos = new_pos;
				ret = 0;
			}
			#endif

			return ret;
		}

		int32_t mkdir(std::string path, mode_t mode)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsMkdir(path.c_str(), mode);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsMkdir(path.c_str(), mode);
			#endif

			#ifdef LINUX
			ret = ::mkdir(path.c_str(), mode);
			#endif

			return ret;
		}

		int32_t rmdir(std::string path)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsRmdir(path.c_str());
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsRmdir(path.c_str());
			#endif

			#ifdef LINUX
			ret = ::rmdir(path.c_str());
			#endif

			return ret;
		}

		int32_t unlink(std::string path)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsUnlink(path.c_str());
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsUnlink(path.c_str());
			#endif

			#ifdef LINUX
			ret = ::unlink(path.c_str());
			#endif

			return ret;
		}

		int32_t stat(std::string path, ftpstat* stat)
		{
			int32_t ret = -1;

			#ifdef CELL_SDK
			ret = cellFsStat(path.c_str(), stat);
			#endif

			#ifdef PSL1GHT_SDK
			ret = sysFsStat(path.c_str(), stat);
			#endif

			#ifdef LINUX
			ret = ::stat(path.c_str(), stat);
			#endif

			return ret;
		}
	}
};

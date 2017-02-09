/* io.hpp: FTP io class definition. */

#pragma once

#include "common.hpp"

namespace FTP
{
	namespace IO
	{
		int32_t open(std::string path, int oflags, int32_t* fd);
		int32_t opendir(std::string path, int32_t* fd);
		int32_t readdir(int32_t fd, ftpdirent* dirent, uint64_t* nread);
		int32_t read(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nread);
		int32_t write(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nwrite);
		int32_t close(int32_t fd);
		int32_t closedir(int32_t fd);
		int32_t rename(std::string old_path, std::string new_path);
		int32_t chmod(std::string path, mode_t mode);
		int32_t lseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos);
		int32_t mkdir(std::string path, mode_t mode);
		int32_t rmdir(std::string path);
		int32_t unlink(std::string path);
		int32_t stat(std::string path, ftpstat* stat);
	};
};

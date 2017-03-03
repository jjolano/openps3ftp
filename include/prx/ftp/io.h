#pragma once

#include "common.h"

int32_t ftpio_open(const char* path, int oflags, int32_t* fd);
int32_t ftpio_opendir(const char* path, int32_t* fd);
int32_t ftpio_readdir(int32_t fd, ftpdirent* dirent, uint64_t* nread);
int32_t ftpio_read(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nread);
int32_t ftpio_write(int32_t fd, char* buf, uint64_t nbytes, uint64_t* nwrite);
int32_t ftpio_close(int32_t fd);
int32_t ftpio_closedir(int32_t fd);
int32_t ftpio_rename(const char* old_path, const char* new_path);
int32_t ftpio_chmod(const char* path, mode_t mode);
int32_t ftpio_lseek(int32_t fd, int64_t offset, int32_t whence, uint64_t* pos);
int32_t ftpio_mkdir(const char* path, mode_t mode);
int32_t ftpio_rmdir(const char* path);
int32_t ftpio_unlink(const char* path);
int32_t ftpio_stat(const char* path, ftpstat* st);
int32_t ftpio_fstat(int32_t fd, ftpstat* st);

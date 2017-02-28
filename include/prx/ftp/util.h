#pragma once

#include "common.h"

struct Directory
{
	char name[256];
};

struct Path
{
	struct Directory* dirname;
	size_t num_levels;
};

void get_absolute_path(char* abs_path, const char* old_path, const char* new_path);

void parse_command_string(char command_name[32], char* command_param, const char* to_parse);
int parse_port_tuple(unsigned short tuple[6], const char* to_parse);

void str_toupper(char* dst, const char* src, size_t len);
bool file_exists(const char* path);

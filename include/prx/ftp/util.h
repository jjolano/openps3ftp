#pragma once

#include "common.h"

struct Directory
{
	char name[MAX_NAME];
};

struct Path
{
	struct Directory* dir;
	size_t num_levels;
};

void get_working_directory(char path_str[MAX_PATH], struct Path* path);
void set_working_directory(struct Path* path, char new_path[MAX_PATH]);
void get_absolute_path(char abs_path[MAX_PATH], const char old_path[MAX_PATH], const char new_path[MAX_PATH]);

void parse_command_string(char command_name[32], char* command_param, char* to_parse);
int parse_port_tuple(unsigned short tuple[6], const char* to_parse);

void get_file_mode(char mode[11], ftpstat* stat);

void str_toupper(char* dst, const char* src, size_t len);
bool file_exists(const char* path);

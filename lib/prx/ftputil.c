#include "util.h"

void get_working_directory(char path_str[MAX_PATH], struct Path* path)
{
	size_t i;

	strcpy(path_str, "/");

	for(i = 0; i < path->num_levels; ++i)
	{
		strcat(path_str, path->dir[i].name);

		if((i + 1) < path->num_levels)
		{
			strcat(path_str, "/");
		}
	}
}

void set_working_directory(struct Path* path, char new_path[MAX_PATH])
{
	size_t o = path->num_levels;
	size_t i = path->num_levels;

	char* dirname = strtok(new_path, "/");

	while(dirname != NULL)
	{
		struct Directory* dir;

		if(i > 0)
		{
			// memory already allocated
			dir = &path->dir[path->num_levels - i--];
		}
		else
		{
			// allocate new memory
			path->dir = (struct Directory*) realloc(path->dir, ++path->num_levels * sizeof(struct Directory));
			dir = &path->dir[path->num_levels - 1];
		}

		strcpy(dir->name, dirname);

		dirname = strtok(NULL, "/");
	}

	if(path->num_levels < o)
	{
		// reallocate memory to fit
		path->dir = (struct Directory*) realloc(path->dir, path->num_levels * sizeof(struct Directory));
	}
}

void get_absolute_path(char abs_path[MAX_PATH], const char old_path[MAX_PATH], const char new_path[MAX_PATH])
{
	if(new_path[0] == '/')
	{
		strcpy(abs_path, new_path);
		return;
	}

	int abs_len;
	strcpy(abs_path, old_path);

	abs_len = strlen(abs_path);

	if(abs_path[abs_len - 1] != '/')
	{
		// add trailing slash
		strcat(abs_path, "/");
	}

	strcat(abs_path, new_path);

	abs_len = strlen(abs_path);

	if(abs_path[abs_len - 1] == '/')
	{
		// remove trailing slash
		abs_path[abs_len - 1] = '\0';
	}
}

void parse_command_string(char command_name[32], char* command_param, char* to_parse)
{
	// initialize output
	command_name[0] = '\0';
	command_param[0] = '\0';

	char* token = strtok(to_parse, " ");

	if(token != NULL)
	{
		strcpy(command_name, token);
	}

	while(token != NULL)
	{
		if(command_param[0] != '\0')
		{
			strcat(command_param, " ");
		}

		strcat(command_param, token);

		token = strtok(NULL, " ");
	}
}

int parse_port_tuple(unsigned short tuple[6], const char* to_parse)
{
	return sscanf(to_parse, "%hu,%hu,%hu,%hu,%hu,%hu",
		&tuple[0],
		&tuple[1],
		&tuple[2],
		&tuple[3],
		&tuple[4],
		&tuple[5]
	);
}

void get_file_mode(char mode[11], CellFsStat st)
{
	mode[0] = '?';

	if((st.st_mode & S_IFMT) == S_IFDIR)
	{
		mode[0] = 'd';
	}

	if((st.st_mode & S_IFMT) == S_IFREG)
	{
		mode[0] = '-';
	}
	
	if((st.st_mode & S_IFMT) == S_IFLNK)
	{
		mode[0] = 'l';
	}

	mode[1] = ((st.st_mode & S_IRUSR) ? 'r' : '-');
	mode[2] = ((st.st_mode & S_IWUSR) ? 'w' : '-');
	mode[3] = ((st.st_mode & S_IXUSR) ? 'x' : '-');
	mode[4] = ((st.st_mode & S_IRGRP) ? 'r' : '-');
	mode[5] = ((st.st_mode & S_IWGRP) ? 'w' : '-');
	mode[6] = ((st.st_mode & S_IXGRP) ? 'x' : '-');
	mode[7] = ((st.st_mode & S_IROTH) ? 'r' : '-');
	mode[8] = ((st.st_mode & S_IWOTH) ? 'w' : '-');
	mode[9] = ((st.st_mode & S_IXOTH) ? 'x' : '-');
	mode[10] = '\0';
}

void str_toupper(char* dst, const char* src)
{
	int c = 0;
 
	do {
		if(src[c] >= 'a' && src[c] <= 'z')
		{
			dst[c] = (src[c] - 32);
		}
		else
		{
			dst[c] = src[c];
		}
	} while(src[c++] != '\0');
}

bool file_exists(const char* path)
{
	CellFsStat st;
	
	if(cellFsStat(path, &st) == 0)
	{
		return true;
	}
	
	return false;
}

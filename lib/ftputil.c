#include "util.h"

/*void get_ntfspath(char* ntfspath, const char* path)
{
	char mount_name[16];

	if(path[10] == '\0')
	{
		strcpy(mount_name, path + 5);
		sprintf(ntfspath, "%s:/", mount_name);
	}

	if(path[10] == '/')
	{
		strncpy(mount_name, path + 5, 5);
		mount_name[5] = '\0';
		sprintf(ntfspath, "%s:%s", mount_name, path + 10);
	}
}*/

struct PTNode* ptnode_init(void)
{
	struct PTNode* n = (struct PTNode*) malloc(sizeof(struct PTNode));

	if(n)
	{
		int i = 26;

		while(i--)
		{
			n->children[i] = NULL;
		}

		n->ptr = NULL;
	}

	return n;
}

void ptnode_insert(struct PTNode* root, const char* key, void* ptr)
{
	int height;
	int length = strlen(key);

	struct PTNode* current = root;

	for(height = 0; height < length; ++height)
	{
		int i = (int) (toupper((int) key[height]) - 'A');

		if(current->children[i] == NULL)
		{
			// create new node
			current->children[i] = ptnode_init();
		}

		// move forward in nodes
		current = current->children[i];
	}

	current->ptr = ptr;
}

struct PTNode* ptnode_nodesearch(struct PTNode* root, const char* key)
{
	int height;
	int length = strlen(key);

	struct PTNode* current = root;

	for(height = 0; height < length; ++height)
	{
		int i = (int) (toupper((int) key[height]) - 'A');

		if(current->children[i] == NULL)
		{
			// partial string doesn't exist, cancel loop early
			return NULL;
		}

		current = current->children[i];
	}

	return current;
}

void* ptnode_search(struct PTNode* root, const char* key)
{
	struct PTNode* n = ptnode_nodesearch(root, key);

	if(n != NULL)
	{
		return n->ptr;
	}

	return NULL;
}

void ptnode_free(struct PTNode* root)
{
	int i;
	for(i = 0; i < 26; ++i)
	{
		if(root->children[i] != NULL)
		{
			ptnode_free(root->children[i]);
		}
	}

	free(root);
}

void get_ntfspath(char* ntfspath, const char* path)
{
	char mount_name[16];
	char* mount_path = strchr((char*) path + 1, '/');
		
	if(mount_path != NULL)
	{
		strncpy(mount_name, path + 5, mount_path - path - 5);
		mount_name[mount_path - path - 5] = '\0';

		sprintf(ntfspath, "%s:%s", mount_name, mount_path);
	}
	else
	{
		strcpy(mount_name, path + 5);
		sprintf(ntfspath, "%s:/", mount_name);
	}
}

void get_working_directory(char path_str[MAX_PATH], struct Path* path)
{
	size_t i;
	size_t len = 1;

	strcpy(path_str, "/");

	for(i = 0; i < path->num_levels; ++i)
	{
		len += strlen(path->dir[i].name);

		if(len > MAX_PATH)
		{
			break;
		}

		strcat(path_str, path->dir[i].name);

		if((i + 1) < path->num_levels)
		{
			len++;
			
			strcat(path_str, "/");
		}
	}
}

void set_working_directory(struct Path* path, char new_path[MAX_PATH])
{
	path->num_levels = 0;

	char* dirname = strtok(new_path, "/");

	while(dirname != NULL)
	{
		// allocate path
		path->dir = (struct Directory*) realloc(path->dir, ++path->num_levels * sizeof(struct Directory));

		struct Directory* dir = &path->dir[path->num_levels - 1];

		strcpy(dir->name, dirname);

		dirname = strtok(NULL, "/");
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

	char* token = strchr(to_parse, ' ');
	int namelen = 0;

	if(token != NULL)
	{
		// has params
		namelen = token - to_parse;
	}
	else
	{
		// no params
		namelen = strlen(to_parse);
	}

	if(namelen > 32)
	{
		namelen = 32;
	}

	// copy strings
	strncpy(command_name, to_parse, namelen);
	command_name[namelen] = '\0';

	if(token != NULL)
	{
		strcpy(command_param, token + 1);
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

void get_file_mode(char mode[11], ftpstat* st)
{
	mode[0] = '?';

	if((st->st_mode & S_IFMT) == S_IFDIR)
	{
		mode[0] = 'd';
	}

	if((st->st_mode & S_IFMT) == S_IFREG)
	{
		mode[0] = '-';
	}
	
	if((st->st_mode & S_IFMT) == S_IFLNK)
	{
		mode[0] = 'l';
	}

	mode[1] = ((st->st_mode & S_IRUSR) ? 'r' : '-');
	mode[2] = ((st->st_mode & S_IWUSR) ? 'w' : '-');
	mode[3] = ((st->st_mode & S_IXUSR) ? 'x' : '-');
	mode[4] = ((st->st_mode & S_IRGRP) ? 'r' : '-');
	mode[5] = ((st->st_mode & S_IWGRP) ? 'w' : '-');
	mode[6] = ((st->st_mode & S_IXGRP) ? 'x' : '-');
	mode[7] = ((st->st_mode & S_IROTH) ? 'r' : '-');
	mode[8] = ((st->st_mode & S_IWOTH) ? 'w' : '-');
	mode[9] = ((st->st_mode & S_IXOTH) ? 'x' : '-');
	mode[10] = '\0';
}

void str_toupper(char* dst, const char* src, size_t len)
{
	size_t c = 0;
 
	do {
		if(src[c] >= 'a' && src[c] <= 'z')
		{
			dst[c] = (src[c] - 32);
		}
		else
		{
			dst[c] = src[c];
		}
	} while(src[c++] != '\0' && c < len);

	dst[c] = '\0';
}

bool file_exists(const char* path)
{
	ftpstat st;
	return ftpio_stat(path, &st) == 0;
}

bool str_startswith(const char* str, const char* sub)
{
	return strncmp(str, sub, strlen(sub)) == 0;
}

#include "ext/ext.h"

void cmd_size(struct Client* client, const char command_name[32], const char* command_params)
{
	void* cvar_cwd_ptr = client_get_cvar(client, "cwd");
	void* cvar_auth_ptr = client_get_cvar(client, "auth");

	struct Path* cwd = (struct Path*) cvar_cwd_ptr;
	bool* auth = (bool*) cvar_auth_ptr;

	if(!*auth)
	{
		client_send_code(client, 530, FTP_530);
		return;
	}

	if(command_params[0] == '\0')
	{
		client_send_code(client, 501, FTP_501);
		return;
	}

	char param_path[MAX_PATH];
	strcpy(param_path, command_params);

	char cwd_str[MAX_PATH];
	get_working_directory(cwd_str, cwd);

	char path[MAX_PATH];
	get_absolute_path(path, cwd_str, param_path);

	CellFsStat st;

	if(cellFsStat(path, &st) == 0)
	{
		char* buffer = client->server_ptr->buffer_data;
		sprintf(buffer, "%" PRIu64, st.st_size);

		client_send_code(client, 213, buffer);
	}
	else
	{
		client_send_code(client, 550, FTP_550);
	}
}

void cmd_mdtm(struct Client* client, const char command_name[32], const char* command_params)
{
	void* cvar_cwd_ptr = client_get_cvar(client, "cwd");
	void* cvar_auth_ptr = client_get_cvar(client, "auth");

	struct Path* cwd = (struct Path*) cvar_cwd_ptr;
	bool* auth = (bool*) cvar_auth_ptr;

	if(!*auth)
	{
		client_send_code(client, 530, FTP_530);
		return;
	}

	if(command_params[0] == '\0')
	{
		client_send_code(client, 501, FTP_501);
		return;
	}

	char param_path[MAX_PATH];
	strcpy(param_path, command_params);

	char cwd_str[MAX_PATH];
	get_working_directory(cwd_str, cwd);

	char path[MAX_PATH];
	get_absolute_path(path, cwd_str, param_path);

	CellFsStat st;

	if(cellFsStat(path, &st) == 0)
	{
		char* buffer = client->server_ptr->buffer_data;
		strftime(buffer, BUFFER_DATA, "%Y%m%d%H%M%S", localtime(&st.st_mtime));

		client_send_code(client, 213, buffer);
	}
	else
	{
		client_send_code(client, 550, FTP_550);
	}
}

void ext_command_import(struct Command* command)
{
	command_register(command, "SIZE", cmd_size);
	command_register(command, "MDTM", cmd_mdtm);
}

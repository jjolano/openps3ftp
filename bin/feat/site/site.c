#include "site/site.h"

void cmd_chmod(struct Client* client, const char command_name[32], const char* command_params)
{
	struct Path* cwd = (struct Path*) client_get_cvar(client, "cwd");
	bool* auth = (bool*) client_get_cvar(client, "auth");

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

	char* params_temp = strdup(command_params);
	char* param = strtok(params_temp, " ");

	if(param == NULL)
	{
		client_send_code(client, 501, FTP_501);
		return;
	}

	mode_t mode = strtoul(param, NULL, 8);

	param = strtok(NULL, " ");

	if(param == NULL)
	{
		client_send_code(client, 501, FTP_501);
		return;
	}

	char param_path[MAX_PATH];
	strcpy(param_path, param);

	char cwd_str[MAX_PATH];
	get_working_directory(cwd_str, cwd);

	char path[MAX_PATH];
	get_absolute_path(path, cwd_str, param_path);

	if(ftpio_chmod(path, mode) == 0)
	{
		client_send_code(client, 200, FTP_200);
	}
	else
	{
		client_send_code(client, 550, FTP_550);
	}

	free(params_temp);
}

void site_command_import(struct Command* command)
{
	command_register(command, "CHMOD", cmd_chmod);
}

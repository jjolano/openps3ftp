#pragma once

#include "common.h"
#include "server.h"
#include "client.h"
#include "command.h"

#define MAX_USERNAME_LEN	32

bool data_list(struct Client* client);
bool data_nlst(struct Client* client);
bool data_retr(struct Client* client);
bool data_stor(struct Client* client);

void cmd_abor(struct Client* client, const char command_name[32], const char* command_params);
void cmd_acct(struct Client* client, const char command_name[32], const char* command_params);
void cmd_allo(struct Client* client, const char command_name[32], const char* command_params);
void cmd_cdup(struct Client* client, const char command_name[32], const char* command_params);
void cmd_cwd(struct Client* client, const char command_name[32], const char* command_params);
void cmd_dele(struct Client* client, const char command_name[32], const char* command_params);
void cmd_help(struct Client* client, const char command_name[32], const char* command_params);
void cmd_list(struct Client* client, const char command_name[32], const char* command_params);
void cmd_mkd(struct Client* client, const char command_name[32], const char* command_params);
void cmd_mode(struct Client* client, const char command_name[32], const char* command_params);
void cmd_nlst(struct Client* client, const char command_name[32], const char* command_params);
void cmd_noop(struct Client* client, const char command_name[32], const char* command_params);
void cmd_pass(struct Client* client, const char command_name[32], const char* command_params);
void cmd_pasv(struct Client* client, const char command_name[32], const char* command_params);
void cmd_port(struct Client* client, const char command_name[32], const char* command_params);
void cmd_pwd(struct Client* client, const char command_name[32], const char* command_params);
void cmd_quit(struct Client* client, const char command_name[32], const char* command_params);
void cmd_rest(struct Client* client, const char command_name[32], const char* command_params);
void cmd_retr(struct Client* client, const char command_name[32], const char* command_params);
void cmd_rmd(struct Client* client, const char command_name[32], const char* command_params);
void cmd_rnfr(struct Client* client, const char command_name[32], const char* command_params);
void cmd_rnto(struct Client* client, const char command_name[32], const char* command_params);
void cmd_site(struct Client* client, const char command_name[32], const char* command_params);
void cmd_stat(struct Client* client, const char command_name[32], const char* command_params);
void cmd_stor(struct Client* client, const char command_name[32], const char* command_params);
void cmd_stru(struct Client* client, const char command_name[32], const char* command_params);
void cmd_syst(struct Client* client, const char command_name[32], const char* command_params);
void cmd_type(struct Client* client, const char command_name[32], const char* command_params);
void cmd_user(struct Client* client, const char command_name[32], const char* command_params);

void base_connect(struct Client* client);
void base_disconnect(struct Client* client);
void base_command_import(struct Command* command);

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "server.h"
#include "client.h"
#include "command.h"

void cmd_size(struct Client* client, const char command_name[32], const char* command_params);
void cmd_mdtm(struct Client* client, const char command_name[32], const char* command_params);
void ext_command_import(struct Command* command);

#ifdef __cplusplus
}
#endif

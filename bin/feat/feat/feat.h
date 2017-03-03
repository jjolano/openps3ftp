#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "server.h"
#include "client.h"
#include "command.h"

void cmd_feat(struct Client* client, const char command_name[32], const char* command_params);
void feat_command_import(struct Command* command);

#ifdef __cplusplus
}
#endif

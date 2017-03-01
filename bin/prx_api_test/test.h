#pragma once

#include "prx.h"
#include "openps3ftp.h"

bool is_module(const char* vsh_module);

void cmd_test(struct Client* client, const char command_name[32], const char* command_params);

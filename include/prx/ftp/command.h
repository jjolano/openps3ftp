#pragma once

#include "common.h"
#include "client.h"

struct CommandCallback
{
	char name[32];
	command_callback callback;
};

struct Command
{
	struct CommandCallback* command_callbacks;
	int num_command_callbacks;

	connect_callback* connect_callbacks;
	int num_connect_callbacks;

	disconnect_callback* disconnect_callbacks;
	int num_disconnect_callbacks;
};

/* command callback functions */
void command_import(struct Command* command, struct Command* ext_command);
bool command_call(struct Command* command, const char name[32], const char* param, struct Client* client);
void command_register(struct Command* command, const char name[32], command_callback callback);

/* connect/disconnect callback functions */
void command_call_connect(struct Command* command, struct Client* client);
void command_call_disconnect(struct Command* command, struct Client* client);
void command_register_connect(struct Command* command, connect_callback callback);
void command_register_disconnect(struct Command* command, disconnect_callback callback);

/* internal functions */
void command_free(struct Command* command);

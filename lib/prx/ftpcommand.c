#include "command.h"

void command_call_connect(struct Command* command, struct Client* client)
{
	int i;
	for(i = 0; i < command->num_connect_callbacks; ++i)
	{
		struct ConnectCallback* cb = &command->connect_callbacks[i];

		if(*cb->callback != NULL)
		{
			(*cb->callback)(client);
		}
	}
}

void command_call_disconnect(struct Command* command, struct Client* client)
{
	int i;
	for(i = 0; i < command->num_disconnect_callbacks; ++i)
	{
		struct DisconnectCallback* cb = &command->disconnect_callbacks[i];

		if(*cb->callback != NULL)
		{
			(*cb->callback)(client);
		}
	}
}

void command_register_connect(struct Command* command, connect_callback callback)
{
	command->connect_callbacks = (struct ConnectCallback*) realloc(command->connect_callbacks, ++command->num_connect_callbacks * sizeof(struct ConnectCallback));

	struct ConnectCallback* cb = &command->connect_callbacks[command->num_connect_callbacks - 1];

	cb->callback = callback;
}

void command_register_disconnect(struct Command* command, disconnect_callback callback)
{
	command->disconnect_callbacks = (struct DisconnectCallback*) realloc(command->disconnect_callbacks, ++command->num_disconnect_callbacks * sizeof(struct DisconnectCallback));

	struct DisconnectCallback* cb = &command->disconnect_callbacks[command->num_disconnect_callbacks - 1];

	cb->callback = callback;
}

bool command_call(struct Command* command, const char name[32], const char* param, struct Client* client)
{
	bool ret = false;
	int i;

	for(i = 0; i < command->num_command_callbacks; ++i)
	{
		struct CommandCallback* cb = &command->command_callbacks[i];

		if(strcmp(cb->name, name) == 0 && *cb->callback != NULL)
		{
			(*cb->callback)(client, name, param);

			// intentionally not breaking loop here since multiple handlers could be a useful feature
			ret = true;
		}
	}

	return ret;
}

void command_register(struct Command* command, const char name[32], command_callback callback)
{
	command->command_callbacks = (struct CommandCallback*) realloc(command->command_callbacks, ++command->num_command_callbacks * sizeof(struct CommandCallback));

	struct CommandCallback* cb = &command->command_callbacks[command->num_command_callbacks - 1];

	strcpy(cb->name, name);
	cb->callback = callback;
}

void command_import(struct Command* command, struct Command* ext_command)
{
	int i;

	// import commands
	for(i = 0; i < ext_command->num_command_callbacks; ++i)
	{
		struct CommandCallback* ext_cb = &ext_command->command_callbacks[i];
		command_register(command, ext_cb->name, ext_cb->callback);
	}

	// import events
	for(i = 0; i < ext_command->num_connect_callbacks; ++i)
	{
		struct ConnectCallback* ext_cb = &ext_command->connect_callbacks[i];
		command_register_connect(command, ext_cb->callback);
	}

	for(i = 0; i < ext_command->num_disconnect_callbacks; ++i)
	{
		struct DisconnectCallback* ext_cb = &ext_command->disconnect_callbacks[i];
		command_register_connect(command, ext_cb->callback);
	}
}

void command_init(struct Command* command)
{
	command->command_callbacks = NULL;
	command->num_command_callbacks = 0;
	command->connect_callbacks = NULL;
	command->num_connect_callbacks = 0;
	command->disconnect_callbacks = NULL;
	command->num_disconnect_callbacks = 0;
}

void command_free(struct Command* command)
{
	if(command->command_callbacks != NULL)
	{
		free(command->command_callbacks);
	}

	if(command->connect_callbacks != NULL)
	{
		free(command->connect_callbacks);
	}

	if(command->disconnect_callbacks != NULL)
	{
		free(command->disconnect_callbacks);
	}
}

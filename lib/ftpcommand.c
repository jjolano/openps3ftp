#include "command.h"

void command_call_connect(struct Command* command, struct Client* client)
{
	if(command->connect_callbacks != NULL)
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
}

void command_call_disconnect(struct Command* command, struct Client* client)
{
	if(command->disconnect_callbacks != NULL)
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
	command_callback callback = ptnode_search(command->command_callbacks, name);

	if(callback != NULL)
	{
		(*callback)(client, name, param);
		return true;
	}

	return false;
}

void command_register(struct Command* command, const char name[32], command_callback callback)
{
	ptnode_insert(command->command_callbacks, name, callback);
}

void command_unregister(struct Command* command, const char name[32])
{
	struct PTNode* n = ptnode_nodesearch(command->command_callbacks, name);

	if(n != NULL)
	{
		n->ptr = NULL;
	}
}

void command_init(struct Command* command)
{
	command->command_callbacks = ptnode_init();

	command->connect_callbacks = NULL;
	command->num_connect_callbacks = 0;

	command->disconnect_callbacks = NULL;
	command->num_disconnect_callbacks = 0;
}

void command_free(struct Command* command)
{
	if(command->command_callbacks != NULL)
	{
		ptnode_free(command->command_callbacks);
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

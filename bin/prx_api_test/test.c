#include "test.h"

void cmd_test(struct Client* client, const char command_name[32], const char* command_params)
{
	client_send_code(client, 200, "Command injection successful!");
}

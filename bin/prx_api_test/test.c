#include "test.h"

void cmd_test(struct Client* client, const char command_name[32], const char* command_params)
{
	sys_prx_id_t ret = prx_get_module_id_by_name(command_params);

	char buf[64];
	sprintf(buf, "prx_get_module_id_by_name: %d", ret);

	client_send_multicode(client, 200, buf);
	client_send_code(client, 200, "Command injection successful!");
}

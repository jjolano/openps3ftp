#include "feat/feat.h"

void cmd_feat(struct Client* client, const char command_name[32], const char* command_params)
{
	client_send_multicode(client, 211, "Features:");

	client_send_multimessage(client, "REST STREAM");
	client_send_multimessage(client, "SIZE");
	client_send_multimessage(client, "MDTM");
	client_send_multimessage(client, "TVFS");

	client_send_code(client, 211, "End");
}

void feat_command_import(struct Command* command)
{
	command_register(command, "FEAT", cmd_feat);
}

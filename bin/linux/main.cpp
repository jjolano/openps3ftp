#include <iostream>
#include <cstdint>
#include <cinttypes>
#include <csignal>

#include "server.h"
#include "client.h"
#include "command.h"

#include "feat/feat.h"
#include "base/base.h"
#include "ext/ext.h"

struct Server ftp_server;

void client_connect(struct Client* client)
{
	std::cout << "Client [fd: " << client->socket_control << "]: connected" << std::endl;
}

void client_disconnect(struct Client* client)
{
	std::cout << "Client [fd: " << client->socket_control << "]: disconnected" << std::endl;
}

void sig_handler(int signo)
{
	if(signo == SIGINT)
	{
		server_stop(&ftp_server);
	}
}

int main(int argc, char* argv[])
{
	signal(SIGINT, sig_handler);

	unsigned short port = 21;

	if(argc == 2)
	{
		port = (unsigned short) atoi(argv[1]);
	}

	struct Command ftp_command;

	// initialize command struct
	command_init(&ftp_command);

	// import commands...
	feat_command_import(&ftp_command);
	base_command_import(&ftp_command);
	ext_command_import(&ftp_command);

	command_register_connect(&ftp_command, client_connect);
	command_register_disconnect(&ftp_command, client_disconnect);
	
	// initialize server struct
	server_init(&ftp_server, &ftp_command, port);

	std::cout << "Started server!" << std::endl;

	uint32_t ret = server_run(&ftp_server);

	std::cout << "Server stopped (code: " << ret << ")!" << std::endl;

	server_free(&ftp_server);
	command_free(&ftp_command);

	return 0;
}

#include <iostream>

#include "common.hpp"
#include "ftp.hpp"
#include "server.hpp"
#include "client.hpp"

void client_connect(FTP::Client* client)
{
	std::cout << "Client [fd: " << client->get_control_socket() << "]: connected" << std::endl;
}

void client_disconnect(FTP::Client* client)
{
	std::cout << "Client [fd: " << client->get_control_socket() << "]: disconnected" << std::endl;
}

int main(void)
{
	FTP::Command command;

	command.register_connect_callback(client_connect);
	command.register_disconnect_callback(client_disconnect);

	FTP::Server server(&command, 21);

	std::cout << "Started server!" << std::endl;

	server.start();

	std::cout << "Server stopped!" << std::endl;

	return 0;
}

#include <iostream>

#include "common.hpp"
#include "server.hpp"
#include "client.hpp"
#include "command.hpp"

#include "feat.hpp"

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
	FTP::Command base_command = feat::base::get_commands();
	FTP::Command app_command = feat::app::get_commands();
	FTP::Command ext_command = feat::ext::get_commands();

	command.import(&base_command);
	command.import(&app_command);
	command.import(&ext_command);

	command.register_connect_callback(client_connect);
	command.register_disconnect_callback(client_disconnect);

	FTP::Server server(&command, 21);

	std::cout << "Started server!" << std::endl;

	server.start();

	std::cout << "Server stopped!" << std::endl;

	return 0;
}

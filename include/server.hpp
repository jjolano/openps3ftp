/* server.hpp: FTP server class definition. */

#pragma once

#include "common.hpp"
#include "client.hpp"
#include "command.hpp"

namespace FTP
{
	class Server
	{
		friend class FTP::Client;

		private:
			FTP::Command* command;

			bool server_running;
			unsigned short server_port;

			int socket_server;
			std::vector<struct pollfd> pollfds;
			std::map<int, FTP::Client*> clients;

		public:
			Server(FTP::Command* ext_command, unsigned short port);
			~Server(void);

			int start(void);
			bool is_running(void);
			int get_num_connections(void);
			void stop(void);
	};
};

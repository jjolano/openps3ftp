/* server.cpp: FTP server class. */

#include "server.hpp"

namespace FTP
{
	Server::Server(FTP::Command* ext_command, unsigned short port)
	{
		command = ext_command;
		server_port = port;
		server_running = false;

		pollfds.reserve(22);
	}

	bool Server::is_running(void)
	{
		return server_running;
	}

	void Server::stop(void)
	{
		server_running = false;
	}

	int Server::get_num_connections(void)
	{
		return clients.size();
	}

	int Server::start(void)
	{
		using namespace std;

		server_running = true;

		socket_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if(socket_server <= 0)
		{
			server_running = false;
			return -1;
		}

		int optval = 1;
		setsockopt(socket_server, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

		sockaddr_in server_addr;
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(server_port);
		server_addr.sin_addr.s_addr = INADDR_ANY;

		#ifdef PS3
		int backlog = 1;
		#else
		int backlog = 10;
		#endif

		if(bind(socket_server, (struct sockaddr*) &server_addr, sizeof(server_addr)) != 0
		|| listen(socket_server, backlog) != 0)
		{
			server_running = false;

			shutdown(socket_server, SHUT_RDWR);
			close(socket_server);

			return -1;
		}

		struct pollfd server_pollfd;
		server_pollfd.fd = PFD(socket_server);
		server_pollfd.events = POLLIN;

		pollfds.push_back(server_pollfd);

		int retval = 0;
		vector<struct pollfd>::reverse_iterator pollfds_it;
		map<int, FTP::Client*>::iterator clients_it;

		while(server_running)
		{
			int p = poll(&pollfds[0], (nfds_t) pollfds.size(), 500);

			if(p == 0)
			{
				continue;
			}

			if(p == -1)
			{
				retval = -2;
				break;
			}

			for(pollfds_it = pollfds.rbegin(); pollfds_it != pollfds.rend(); ++pollfds_it)
			{
				if(p == 0)
				{
					break;
				}

				struct pollfd pfd = *pollfds_it;
				int sfd = OFD(pfd.fd);

				if(pfd.revents)
				{
					p--;

					if(pfd.revents & POLLNVAL)
					{
						pollfds.erase(--pollfds_it.base());
						continue;
					}

					if(sfd == socket_server)
					{
						if(pfd.revents & (POLLERR|POLLHUP))
						{
							server_running = false;
							retval = -3;
							break;
						}

						int socket_client = accept(socket_server, NULL, NULL);

						if(socket_client <= 0)
						{
							continue;
						}

						FTP::Client* client = new (nothrow) FTP::Client(socket_client, this);

						if(!client)
						{
							shutdown(socket_client, SHUT_RDWR);
							close(socket_client);
							continue;
						}

						struct pollfd client_pollfd;
						client_pollfd.fd = PFD(socket_client);
						client_pollfd.events = (POLLIN|POLLRDNORM);

						clients.insert(make_pair(socket_client, client));
						pollfds.push_back(client_pollfd);

						client->send_multicode(220, WELCOME_MSG);
						client->send_code(220, "Ready.");
					}
					else
					{
						clients_it = clients.find(sfd);

						if(clients_it == clients.end())
						{
							pollfds.erase(--pollfds_it.base());
							continue;
						}

						FTP::Client* client = clients_it->second;

						if(pfd.revents & (POLLERR|POLLHUP))
						{
							if(sfd == client->get_control_socket())
							{
								// client disconnect
								delete client;
							}
							else
							{
								// data disconnect
								client->socket_disconnect(sfd);
							}

							clients.erase(clients_it);
							pollfds.erase(--pollfds_it.base());

							continue;
						}

						client->socket_event(sfd);
					}
				}
			}
		}

		shutdown(socket_server, SHUT_RDWR);
		close(socket_server);

		for(clients_it = clients.begin(); clients_it != clients.end(); ++clients_it)
		{
			FTP::Client* client = clients_it->second;
			delete client;
		}

		pollfds.clear();
		clients.clear();

		server_running = false;

		return retval;
	}

	Server::~Server(void)
	{
		stop();
	}
};

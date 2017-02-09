/* client.cpp: FTP client class. */

#include "client.hpp"

namespace FTP
{
	Client::Client(int socket_client, FTP::Server* ext_server)
	{
		server = ext_server;
		socket_control = socket_client;
		socket_data = -1;
		socket_pasv = -1;

		buffer_control = NULL;
		buffer_data = NULL;
		cb_data = NULL;

		struct linger optlinger;
		optlinger.l_onoff = 1;
		optlinger.l_linger = 3;
		setsockopt(socket_control, SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

		struct pollfd client_pollfd;
		client_pollfd.fd = socket_control;
		client_pollfd.events = (POLLIN|POLLRDNORM);

		server->clients.insert(std::make_pair(socket_control, this));
		server->pollfds.push_back(client_pollfd);
	}

	void Client::send_message(std::string message)
	{
		message += '\r';
		message += '\n';

		send(socket_control, message.c_str(), message.size(), 0);
	}

	void Client::send_code(int code, std::string message)
	{
		std::stringstream ss_message;
		ss_message << code << ' ' << message;
		send_message(ss_message.str());
	}

	void Client::send_multicode(int code, std::string message)
	{
		std::stringstream ss_message;
		ss_message << code << '-' << message;
		send_message(ss_message.str());
	}

	void Client::send_multimessage(std::string message)
	{
		message = ' ' + message;
		send_message(message);
	}

	bool Client::data_start(data_callback callback, short pevents)
	{
		if(socket_data <= 0)
		{
			// check socket_pasv
			if(socket_pasv > 0)
			{
				std::vector<struct pollfd> pollfds_pasv;

				struct pollfd pasv_pollfd;
				pasv_pollfd.fd = socket_pasv;
				pasv_pollfd.events = POLLIN;

				pollfds_pasv.push_back(pasv_pollfd);

				int p = poll(&pollfds_pasv[0], 1, 3000);

				if(p <= 0)
				{
					return false;
				}

				socket_data = accept(socket_pasv, NULL, NULL);

				socket_disconnect(socket_pasv);

				if(socket_data <= 0)
				{
					return false;
				}
			}
			else
			{
				void* cvar_ptr = get_cvar("port_addr");
				struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;

				struct sockaddr_in active_addr;
				socklen_t len = sizeof(struct sockaddr_in);

				if(cvar_ptr != NULL)
				{
					// port
					memcpy(&active_addr, port_addr, len);

					delete port_addr;
					set_cvar("port_addr", NULL);
				}
				else
				{
					// legacy port
					getpeername(socket_control, (struct sockaddr*) &active_addr, &len);
					active_addr.sin_port = htons(20);
				}

				socket_data = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

				if(socket_data <= 0)
				{
					return false;
				}

				if(connect(socket_data, (struct sockaddr*) &active_addr, len) != 0)
				{
					socket_disconnect(socket_data);
					return false;
				}
			}
		}

		buffer_data = new char[BUFFER_DATA];

		int optval = BUFFER_DATA;
		setsockopt(socket_data, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
		setsockopt(socket_data, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));

		struct linger optlinger;
		optlinger.l_onoff = 1;
		optlinger.l_linger = 3;
		setsockopt(socket_data, SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

		struct pollfd data_pollfd;
		data_pollfd.fd = socket_data;
		data_pollfd.events = (pevents | POLLIN);

		server->pollfds.push_back(data_pollfd);
		server->clients.insert(std::make_pair(socket_data, this));

		cb_data = callback;
		return true;
	}

	void Client::data_end(void)
	{
		socket_disconnect(socket_data);
		delete buffer_data;
		buffer_data = NULL;
		cb_data = NULL;
	}

	bool Client::pasv_enter(struct sockaddr_in* pasv_addr)
	{
		data_end();
		socket_disconnect(socket_pasv);

		socket_pasv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		if(socket_pasv <= 0)
		{
			return false;
		}

		struct linger optlinger;
		optlinger.l_onoff = 1;
		optlinger.l_linger = 3;
		setsockopt(socket_pasv, SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

		socklen_t len = sizeof(struct sockaddr_in);

		getsockname(socket_control, (struct sockaddr*) pasv_addr, &len);

		pasv_addr->sin_port = 0;

		if(bind(socket_pasv, (struct sockaddr*) pasv_addr, len) != 0
		|| listen(socket_pasv, 1) != 0)
		{
			socket_disconnect(socket_pasv);
			return false;
		}

		getsockname(socket_pasv, (struct sockaddr*) pasv_addr, &len);

		return true;
	}

	void* Client::get_cvar(std::string name)
	{
		using namespace std;

		map<string, void*>::iterator cvar_it;
		cvar_it = cvar.find(name);

		if(cvar_it != cvar.end())
		{
			return cvar_it->second;
		}

		return NULL;
	}

	void Client::set_cvar(std::string name, void* value_ptr)
	{
		if(value_ptr == NULL)
		{
			using namespace std;

			map<string, void*>::iterator cvar_it;
			cvar_it = cvar.find(name);

			if(cvar_it != cvar.end())
			{
				cvar.erase(cvar_it);
			}
		}
		else
		{
			cvar[name] = value_ptr;
		}
	}

	bool Client::socket_disconnect(int socket_dc)
	{
		if(socket_dc > 0)
		{
			shutdown(socket_dc, SHUT_RDWR);
			close(socket_dc);

			if(socket_dc == socket_pasv)
			{
				socket_pasv = -1;
			}

			if(socket_dc == socket_data
			|| socket_dc == socket_control)
			{
				{
					using namespace std;

					vector<struct pollfd>::iterator pollfds_it;
					map<int, FTP::Client*>::iterator clients_it;

					clients_it = server->clients.find(socket_dc);

					for(pollfds_it = server->pollfds.begin(); pollfds_it != server->pollfds.end(); ++pollfds_it)
					{
						struct pollfd pfd = *pollfds_it;

						if(pfd.fd == socket_dc)
						{
							server->pollfds.erase(pollfds_it);
							break;
						}
					}

					if(clients_it != server->clients.end())
					{
						server->clients.erase(clients_it);
					}
				}

				if(socket_dc == socket_data)
				{
					if(buffer_data != NULL)
					{
						data_end();
					}

					socket_data = -1;
				}
				else
				{
					socket_disconnect(socket_pasv);
					data_end();
					return true;
				}
			}
		}

		return false;
	}

	void Client::socket_event(int socket_ev)
	{
		if(socket_ev > 0)
		{
			if(socket_ev == socket_data)
			{
				bool ended = (*cb_data)(this);

				if(ended)
				{
					data_end();
				}
			}

			if(socket_ev == socket_control)
			{
				buffer_control = new char[BUFFER_CONTROL];
				recv(socket_ev, buffer_control, BUFFER_CONTROL, 0);

				std::pair<std::string, std::string> command;
				command = FTP::Utilities::parse_command_string(buffer_control);

				if(!server->command->call_command(command, this))
				{
					send_code(502, FTP_502);
				}

				delete buffer_control;
				buffer_control = NULL;
			}
		}
	}

	Client::~Client(void)
	{
		socket_disconnect(socket_pasv);
		socket_disconnect(socket_data);
		socket_disconnect(socket_control);

		if(buffer_control != NULL)
		{
			delete buffer_control;
		}

		if(buffer_data != NULL)
		{
			delete buffer_data;
		}
	}
};

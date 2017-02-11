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

		buffer_control = new (std::nothrow) char[BUFFER_CONTROL];
		buffer_data = new (std::nothrow) char[BUFFER_DATA];
		cb_data = NULL;

		struct linger optlinger;
		optlinger.l_onoff = 1;
		optlinger.l_linger = 1;
		setsockopt(socket_control, SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

		server->command->call_connect(this);
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
				pasv_pollfd.fd = PFD(socket_pasv);
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

				struct sockaddr_in active_addr;
				socklen_t len = sizeof(struct sockaddr_in);

				if(cvar_ptr != NULL)
				{
					// port
					struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;
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

		int optval = BUFFER_DATA;
		setsockopt(socket_data, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
		setsockopt(socket_data, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));

		struct linger optlinger;
		optlinger.l_onoff = 1;
		optlinger.l_linger = 1;
		setsockopt(socket_data, SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

		struct pollfd data_pollfd;
		data_pollfd.fd = PFD(socket_data);
		data_pollfd.events = (pevents | POLLIN);

		server->clients.insert(std::make_pair(socket_data, this));
		server->pollfds.push_back(data_pollfd);

		cb_data = callback;
		return true;
	}

	void Client::data_end(void)
	{
		if(socket_data > 0)
		{
			shutdown(socket_data, SHUT_RDWR);
			cb_data = NULL;

			#ifdef PS3
			{
				using namespace std;

				vector<struct pollfd>::iterator pollfds_it;
				
				for(pollfds_it = server->pollfds.begin(); pollfds_it != server->pollfds.end(); ++pollfds_it)
				{
					if(OFD(pollfds_it->fd) == socket_data)
					{
						server->pollfds.erase(pollfds_it);
						break;
					}
				}

				server->clients.erase(server->clients.find(socket_data));
			}

			close(socket_data);
			socket_data = -1;
			#endif
		}
	}

	bool Client::pasv_enter(struct sockaddr_in* pasv_addr)
	{
		void* cvar_ptr = get_cvar("port_addr");

		if(cvar_ptr != NULL)
		{
			struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;
			delete port_addr;
			set_cvar("port_addr", NULL);
		}

		if(socket_pasv > 0)
		{
			socket_disconnect(socket_pasv);
		}

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

	int Client::get_control_socket(void)
	{
		return socket_control;
	}

	int Client::get_data_socket(void)
	{
		return socket_data;
	}

	void Client::socket_disconnect(int socket_dc)
	{
		if(socket_dc > 0)
		{
			close(socket_dc);

			if(socket_dc == socket_pasv)
			{
				socket_pasv = -1;
			}

			if(socket_dc == socket_data)
			{
				data_end();
				socket_data = -1;
			}

			if(socket_dc == socket_control)
			{
				if(socket_pasv > 0)
				{
					close(socket_pasv);
				}

				if(socket_data > 0)
				{
					data_end();
				}

				socket_control = -1;
			}
		}
	}

	void Client::socket_event(int socket_ev)
	{
		if(socket_ev > 0)
		{
			if(socket_ev == socket_data)
			{
				bool ended = (*cb_data)(this, socket_data);

				if(ended)
				{
					data_end();
				}
			}

			if(socket_ev == socket_control)
			{
				ssize_t bytes = recv(socket_ev, buffer_control, BUFFER_CONTROL, 0);

				if(bytes <= 0)
				{
					shutdown(socket_ev, SHUT_RDWR);

					#ifdef PS3
					{
						using namespace std;

						vector<struct pollfd>::iterator pollfds_it;
						
						for(pollfds_it = server->pollfds.begin(); pollfds_it != server->pollfds.end(); ++pollfds_it)
						{
							if(OFD(pollfds_it->fd) == socket_ev)
							{
								server->pollfds.erase(pollfds_it);
								break;
							}
						}

						server->clients.erase(server->clients.find(socket_ev));
					}

					delete this;
					#endif

					return;
				}

				if(bytes <= 2)
				{
					return;
				}

				buffer_control[bytes - 2] = '\0';

				std::pair<std::string, std::string> command;
				command = FTP::Utilities::parse_command_string(buffer_control);

				if(!server->command->call_command(command, this))
				{
					send_code(502, FTP_502);
				}

				last_cmd = command.first;
			}
		}
	}

	Client::~Client(void)
	{
		server->command->call_disconnect(this);

		socket_disconnect(socket_control);

		if(buffer_control != NULL)
		{
			delete[] buffer_control;
			buffer_control = NULL;
		}

		if(buffer_data != NULL)
		{
			delete[] buffer_data;
			buffer_data = NULL;
		}
	}
};

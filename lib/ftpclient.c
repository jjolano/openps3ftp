#include "client.h"

void* client_get_cvar(struct Client* client, const char name[32])
{
	int i;

	for(i = 0; i < client->cvar_count; ++i)
	{
		struct ClientVariable* cvar_ptr = &client->cvar[i];

		if(strcmp(cvar_ptr->name, name) == 0)
		{
			return cvar_ptr->ptr;
		}
	}

	return NULL;
}

void client_set_cvar(struct Client* client, const char name[32], void* ptr)
{
	int i;

	for(i = 0; i < client->cvar_count; ++i)
	{
		struct ClientVariable* cvar_ptr = &client->cvar[i];

		if(strcmp(cvar_ptr->name, name) == 0)
		{
			cvar_ptr->ptr = ptr;
			return;
		}
	}

	// create new cvar
	client->cvar = (struct ClientVariable*) realloc(client->cvar, ++client->cvar_count * sizeof(struct ClientVariable));

	struct ClientVariable* cvar_ptr = &client->cvar[client->cvar_count - 1];

	strcpy(cvar_ptr->name, name);
	cvar_ptr->ptr = ptr;
}

void client_send_message(struct Client* client, const char* message)
{
	char* buffer = client->server_ptr->buffer_control;
	size_t len = sprintf(buffer, "%s\r\n", message);

	if(send(client->socket_control, buffer, len, 0) != 0)
	{
		client_socket_disconnect(client, client->socket_control);
	}
}

void client_send_code(struct Client* client, int code, const char* message)
{
	char* buffer = client->server_ptr->buffer_control;
	size_t len = sprintf(buffer, "%d %s\r\n", code, message);

	if(send(client->socket_control, buffer, len, 0) != 0)
	{
		client_socket_disconnect(client, client->socket_control);
	}
}

void client_send_multicode(struct Client* client, int code, const char* message)
{
	char* buffer = client->server_ptr->buffer_control;
	size_t len = sprintf(buffer, "%d-%s\r\n", code, message);

	if(send(client->socket_control, buffer, len, 0) != 0)
	{
		client_socket_disconnect(client, client->socket_control);
	}
}

void client_send_multimessage(struct Client* client, const char* message)
{
	char* buffer = client->server_ptr->buffer_control;
	size_t len = sprintf(buffer, " %s\r\n", message);

	if(send(client->socket_control, buffer, len, 0) != 0)
	{
		client_socket_disconnect(client, client->socket_control);
	}
}

bool client_data_start(struct Client* client, data_callback callback, short pevents)
{
	if(client->socket_data == -1)
	{
		if(client->socket_pasv == -1)
		{
			// try port
			void* cvar_ptr = client_get_cvar(client, "port_addr");

			struct sockaddr_in active_addr;
			socklen_t len = sizeof(struct sockaddr_in);

			if(cvar_ptr != NULL)
			{
				// port
				struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;
				memcpy(&active_addr, port_addr, len);

				free(port_addr);
				client_set_cvar(client, "port_addr", NULL);
			}
			else
			{
				// legacy port
				getpeername(client->socket_control, (struct sockaddr*) &active_addr, &len);
				active_addr.sin_port = htons(20);
			}

			client->socket_data = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if(client->socket_data < 0)
			{
				client->socket_data = -1;
				return false;
			}

			int optval = BUFFER_DATA;
			setsockopt(client->socket_data, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
			setsockopt(client->socket_data, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));

			struct timeval opttv;
			opttv.tv_sec = 5;
			opttv.tv_usec = 0;

			setsockopt(client->socket_data, SOL_SOCKET, SO_SNDTIMEO, &opttv, sizeof(opttv));

			if(connect(client->socket_data, (struct sockaddr*) &active_addr, len) != 0)
			{
				socketclose(client->socket_data);
				client->socket_data = -1;
				return false;
			}
		}
		else
		{
			// try pasv
			struct pollfd pasv_pollfd;
			pasv_pollfd.fd = client->socket_pasv;
			pasv_pollfd.events = POLLIN;

			int p = socketpoll(&pasv_pollfd, 1, 5000);

			if(p <= 0)
			{
				socketclose(client->socket_pasv);
				client->socket_pasv = -1;
				return false;
			}

			client->socket_data = accept(client->socket_pasv, NULL, NULL);

			socketclose(client->socket_pasv);
			client->socket_pasv = -1;

			if(client->socket_data < 0)
			{
				return false;
			}

			int optval = BUFFER_DATA;
			setsockopt(client->socket_data, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
			setsockopt(client->socket_data, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));

			struct timeval opttv;
			opttv.tv_sec = 5;
			opttv.tv_usec = 0;

			setsockopt(client->socket_data, SOL_SOCKET, SO_SNDTIMEO, &opttv, sizeof(opttv));
		}
	}

	struct linger optlinger;
	optlinger.l_onoff = 1;
	optlinger.l_linger = 10;

	setsockopt(client->socket_data, SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));


	//int optval = 1;
	//setsockopt(client->socket_data, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

	server_pollfds_add(client->server_ptr, client->socket_data, pevents|POLLIN);
	server_client_add(client->server_ptr, client->socket_data, &client);

	client->cb_data = callback;
	return true;
}

void client_data_end(struct Client* client)
{
	if(client->socket_data != -1)
	{
		client->cb_data = NULL;

		server_pollfds_remove(client->server_ptr, client->socket_data);
		server_client_remove(client->server_ptr, client->socket_data);

		void* cvar_fd_ptr = client_get_cvar(client, "fd");

		if(cvar_fd_ptr != NULL)
		{
			int* fd = (int*) cvar_fd_ptr;

			if(*fd != -1)
			{
				ftpstat st;
				
				if(ftpio_fstat(*fd, &st) == 0)
				{
					if((st.st_mode & S_IFMT) == S_IFDIR)
					{
						ftpio_closedir(*fd);
					}
					else
					{
						ftpio_close(*fd);
					}
				}

				*fd = -1;
			}
		}

		shutdown(client->socket_data, SHUT_RDWR);
		socketclose(client->socket_data);

		client->socket_data = -1;
	}
}

bool client_pasv_enter(struct Client* client, struct sockaddr_in* pasv_addr)
{
	void* cvar_ptr = client_get_cvar(client, "port_addr");

	if(cvar_ptr != NULL)
	{
		struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;
		free(port_addr);
		client_set_cvar(client, "port_addr", NULL);
	}

	if(client->socket_pasv != -1)
	{
		socketclose(client->socket_pasv);
	}

	client->socket_pasv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if(client->socket_pasv < 0)
	{
		client->socket_pasv = -1;
		return false;
	}

	int optval = 1;
	setsockopt(client->socket_pasv, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	setsockopt(client->socket_pasv, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	struct linger optlinger;
	optlinger.l_onoff = 1;
	optlinger.l_linger = 0;
	setsockopt(client->socket_pasv, SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

	socklen_t len = sizeof(struct sockaddr_in);

	getsockname(client->socket_control, (struct sockaddr*) pasv_addr, &len);

	pasv_addr->sin_port = 0;

	if(bind(client->socket_pasv, (struct sockaddr*) pasv_addr, len) != 0)
	{
		socketclose(client->socket_pasv);
		client->socket_pasv = -1;
		return false;
	}

	listen(client->socket_pasv, 1);

	getsockname(client->socket_pasv, (struct sockaddr*) pasv_addr, &len);

	return true;
}

void client_socket_event(struct Client* client, int socket_ev)
{
	if(socket_ev == -1)
	{
		return;
	}

	if(socket_ev == client->socket_data)
	{
		if(client->cb_data != NULL)
		{
			bool ended = (*client->cb_data)(client);

			if(ended)
			{
				client_data_end(client);
			}
		}
		else
		{
			client_data_end(client);
		}
	}

	if(socket_ev == client->socket_control)
	{
		char* buffer = client->server_ptr->buffer_control;

		ssize_t bytes = recv(socket_ev, buffer, BUFFER_CONTROL, 0);

		if(bytes <= 0)
		{
			client_socket_disconnect(client, socket_ev);

			server_pollfds_remove(client->server_ptr, socket_ev);
			server_client_remove(client->server_ptr, socket_ev);
			return;
		}

		if(bytes <= 2)
		{
			return;
		}

		buffer[bytes - 2] = '\0';

		char command_name[32];
		char* command_param = client->server_ptr->buffer_command;

		parse_command_string(command_name, command_param, buffer);

		if(!command_call(client->server_ptr->command_ptr, command_name, command_param, client))
		{
			client_send_code(client, 502, FTP_502);
		}
		else
		{
			strcpy(client->lastcmd, command_name);
		}
	}
}

void client_socket_disconnect(struct Client* client, int socket_dc)
{
	if(socket_dc == -1)
	{
		return;
	}

	if(socket_dc == client->socket_data)
	{
		client_data_end(client);
	}

	if(socket_dc == client->socket_control)
	{
		client_data_end(client);

		if(client->socket_pasv != -1)
		{
			socketclose(client->socket_pasv);
		}

		shutdown(client->socket_control, SHUT_RDWR);
		socketclose(client->socket_control);
	}
}

void client_free(struct Client* client)
{
	if(client->cvar != NULL)
	{
		free(client->cvar);
	}
}

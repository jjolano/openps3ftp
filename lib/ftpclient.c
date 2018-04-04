#include "client.h"
#include "sys.thread.h"

void* client_get_cvar(struct Client* client, const char* name)
{
	struct PTNode* n = pttree_search(client->cvar, name);
	return n != NULL ? n->data_ptr : NULL;
}

void client_set_cvar(struct Client* client, const char* name, void* ptr)
{
	pttree_insert(client->cvar, name, ptr);
}

void client_send_message(struct Client* client, const char* message)
{
	char* buffer = client->buffer_control;
	size_t len = sprintf(buffer, "%s\r\n", message);

	if(send(client->socket_control, buffer, len, 0) < 0)
	{
		client_socket_disconnect(client, client->socket_control);

		server_pollfds_remove(client->server_ptr, client->socket_control);
		server_client_remove(client->server_ptr, client->socket_control);
	}
}

void client_send_code(struct Client* client, int code, const char* message)
{
	char* buffer = client->buffer_control;
	size_t len = sprintf(buffer, "%d %s\r\n", code, message);

	if(send(client->socket_control, buffer, len, 0) < 0)
	{
		client_socket_disconnect(client, client->socket_control);

		server_pollfds_remove(client->server_ptr, client->socket_control);
		server_client_remove(client->server_ptr, client->socket_control);
	}
}

void client_send_multicode(struct Client* client, int code, const char* message)
{
	char* buffer = client->buffer_control;
	size_t len = sprintf(buffer, "%d-%s\r\n", code, message);

	if(send(client->socket_control, buffer, len, 0) < 0)
	{
		client_socket_disconnect(client, client->socket_control);

		server_pollfds_remove(client->server_ptr, client->socket_control);
		server_client_remove(client->server_ptr, client->socket_control);
	}
}

void client_send_multimessage(struct Client* client, const char* message)
{
	char* buffer = client->buffer_control;
	size_t len = sprintf(buffer, " %s\r\n", message);

	if(send(client->socket_control, buffer, len, 0) < 0)
	{
		client_socket_disconnect(client, client->socket_control);

		server_pollfds_remove(client->server_ptr, client->socket_control);
		server_client_remove(client->server_ptr, client->socket_control);
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
		}
	}

	int optval = 1;
	setsockopt(client->socket_data, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

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

int client_socket_event(struct Client* client, int socket_ev)
{
	if(socket_ev == -1)
	{
		return 0;
	}

	if(socket_ev == client->socket_data)
	{
		if(client->cb_data != NULL)
		{
			bool ended = (*client->cb_data)(client);

			if(ended)
			{
				client_data_end(client);
				return 0;
			}
		}
		else
		{
			client_data_end(client);
			return 0;
		}

		return 0;
	}

	if(socket_ev == client->socket_control)
	{
		char* buffer = client->buffer_control;

		ssize_t bytes = recv(socket_ev, buffer, BUFFER_CONTROL, 0);

		if(bytes <= 2)
		{
			client_socket_disconnect(client, socket_ev);

			server_pollfds_remove(client->server_ptr, socket_ev);
			server_client_remove(client->server_ptr, socket_ev);
			return 1;
		}

		buffer[bytes - 2] = '\0';

		char command_name[32];
		char* command_param = client->buffer_command;

		parse_command_string(command_name, command_param, buffer);

		if(strcmp(command_name, "QUIT") == 0)
		{
			client_send_code(client, 221, FTP_221);
			client_socket_disconnect(client, socket_ev);

			server_pollfds_remove(client->server_ptr, socket_ev);
			server_client_remove(client->server_ptr, socket_ev);
			return 1;
		}

		if(!command_call(client->server_ptr->command_ptr, command_name, command_param, client))
		{
			client_send_code(client, 502, FTP_502);
		}
		else
		{
			strcpy(client->lastcmd, command_name);
		}
	}

	return 0;
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
		pttree_destroy(client->cvar);
	}

	if(client->mutex != NULL)
	{
		sys_thread_mutex_destroy(client->mutex);
		sys_thread_mutex_free(client->mutex);
	}

	if(client->buffer_control != NULL)
	{
		free(client->buffer_control);
	}

	if(client->buffer_data != NULL)
	{
		free(client->buffer_data);
	}

	if(client->buffer_command != NULL)
	{
		free(client->buffer_command);
	}
}

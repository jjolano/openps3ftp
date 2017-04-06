#include "server.h"

void server_pollfds_add(struct Server* server, int fd, short events)
{
	// allocate new pollfd
	server->pollfds = (struct pollfd*) realloc(server->pollfds, ++server->nfds * sizeof(struct pollfd));
	
	// set pollfd parameters
	struct pollfd* pfd = &server->pollfds[server->nfds - 1];
	
	pfd->fd = fd;
	pfd->events = events;
}

void server_pollfds_remove(struct Server* server, int fd)
{
	if(server->pollfds != NULL)
	{
		// find index
		nfds_t i;

		for(i = 0; i < server->nfds; ++i)
		{
			if(server->pollfds[i].fd == fd)
			{
				break;
			}
		}

		if(i == server->nfds)
		{
			// not found, don't do anything
			return;
		}

		// remove element
		server->pollfds[i] = server->pollfds[server->nfds - 1];

		// reallocate memory
		server->pollfds = (struct pollfd*) realloc(server->pollfds, --server->nfds * sizeof(struct pollfd));
	}
}

void server_client_add(struct Server* server, int fd, struct Client** client_ptr)
{
	if(*client_ptr != NULL)
	{
		// allocate client for data connection
		avltree_insert(server->clients, fd, *client_ptr);
		return;
	}
	
	// initialize new client
	*client_ptr = (struct Client*) malloc(sizeof(struct Client));

	struct Client* client = *client_ptr;

	if(client == NULL)
	{
		return;
	}

	client->server_ptr = server;

	client->cvar = pttree_create();

	client->socket_control = fd;
	client->socket_data = -1;
	client->socket_pasv = -1;

	client->cb_data = NULL;
	client->lastcmd[0] = '\0';

	// add to nodes
	avltree_insert(server->clients, fd, client);

	// call connect callback
	command_call_connect(server->command_ptr, client);
}

void server_client_find(struct Server* server, int fd, struct Client** client_ptr)
{
	*client_ptr = NULL;

	struct AVLNode* n = avltree_search(server->clients, fd);

	if(n)
	{
		*client_ptr = n->data_ptr;
	}
}

void server_client_remove(struct Server* server, int fd)
{
	struct Client* client = NULL;
	server_client_find(server, fd, &client);

	if(client != NULL)
	{
		if(client->socket_control == fd)
		{
			// free client if control socket
			// call disconnect callback
			command_call_disconnect(server->command_ptr, client);

			// make sure client is disconnected
			client_socket_disconnect(client, fd);

			client_free(client);
			free(client);
		}

		avltree_remove(server->clients, fd);
	}
}

void server_init(struct Server* server, struct Command* command_ptr, unsigned short port)
{
	// assuming server is allocated memory

	server->command_ptr = command_ptr;
	server->port = port;
	server->running = false;
	server->should_stop = false;

	server->buffer_control = (char*) malloc(BUFFER_CONTROL * sizeof(char));
	server->buffer_data = (char*) malloc(BUFFER_DATA * sizeof(char));
	server->buffer_command = (char*) malloc(BUFFER_COMMAND * sizeof(char));
	server->pollfds = NULL;
	server->clients = avltree_create();

	server->nfds = 0;

	server->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

uint32_t server_run(struct Server* server)
{
	// assuming server is initialized using server_init
	if(server->socket < 0)
	{
		// socket failed to create
		return 1;
	}

	// setup server socket
	int optval = 1;
	setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	setsockopt(server->socket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server->port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(server->socket, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in)) != 0)
	{
		// failed to bind socket to port
		socketclose(server->socket);
		return 1;
	}

	listen(server->socket, 10);

	// add to pollfds
	server_pollfds_add(server, server->socket, POLLIN);

	server->running = true;
	server->should_stop = false;

	// main server loop
	int retval = 0;

	while(!server->should_stop)
	{
		int p = socketpoll(server->pollfds, server->nfds, 500);

		if(p == 0)
		{
			continue;
		}

		if(p < 0)
		{
			retval = 2;
			break;
		}

		nfds_t i = server->nfds;

		while(p > 0 && i--)
		{
			struct pollfd* pfd = &server->pollfds[i];

			if(pfd->revents)
			{
				p--;

				if(pfd->revents & POLLNVAL)
				{
					server_pollfds_remove(server, pfd->fd);
					continue;
				}

				if(pfd->fd == server->socket)
				{
					if(pfd->revents & (POLLERR|POLLHUP))
					{
						retval = 3;
						break;
					}

					int socket_client = accept(server->socket, NULL, NULL);

					if(socket_client < 0)
					{
						continue;
					}

					struct timeval opttv;
					opttv.tv_sec = 5;
					opttv.tv_usec = 0;
					setsockopt(socket_client, SOL_SOCKET, SO_SNDTIMEO, &opttv, sizeof(opttv));

					struct linger optlinger;
					optlinger.l_onoff = 1;

					#ifndef LINUX
					optlinger.l_linger = 0;
					#else
					optlinger.l_linger = 15;
					#endif
					
					setsockopt(socket_client, SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

					setsockopt(socket_client, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

					struct Client* client = NULL;
					server_client_add(server, socket_client, &client);

					if(client != NULL)
					{
						server_pollfds_add(server, socket_client, POLLIN|POLLRDNORM);
						client_send_code(client, 220, "FTP Ready.");
					}
					else
					{
						socketclose(socket_client);
					}
				}
				else
				{
					struct Client* client = NULL;
					server_client_find(server, pfd->fd, &client);

					if(client == NULL)
					{
						// remove orphan socket
						socketclose(pfd->fd);
						server_pollfds_remove(server, pfd->fd);
						continue;
					}

					// handle disconnections
					if(pfd->revents & (POLLERR|POLLHUP)
					|| (
						pfd->events & POLLOUT &&
						pfd->revents & POLLIN &&
						recv(pfd->fd, server->buffer_data, BUFFER_DATA, MSG_PEEK) <= 0
					))
					{
						client_socket_disconnect(client, pfd->fd);

						if(pfd->fd == client->socket_control)
						{
							// control connection disconnected, remove from clients
							server_client_remove(server, pfd->fd);
						}

						// remove from pollfds
						server_pollfds_remove(server, pfd->fd);
						continue;
					}

					// let client handle socket events
					client_socket_event(client, pfd->fd);
				}
			}
		}
	}

	server->running = false;

	socketclose(server->socket);
	server->socket = -1;

	// clear clients
	while(server->clients->root != NULL)
	{
		server_client_remove(server, server->clients->root->key);
	}

	// clear pollfds
	while(server->nfds > 0)
	{
		server_pollfds_remove(server, server->pollfds[0].fd);
	}

	return retval;
}

void server_stop(struct Server* server)
{
	// set flag
	server->should_stop = true;
}

void server_free(struct Server* server)
{
	// assuming server->running = false

	// free memory
	if(server->pollfds != NULL)
	{
		free(server->pollfds);
	}

	if(server->clients != NULL)
	{
		avltree_destroy(server->clients);
	}

	if(server->buffer_control != NULL)
	{
		free(server->buffer_control);
	}

	if(server->buffer_data != NULL)
	{
		free(server->buffer_data);
	}

	if(server->buffer_command != NULL)
	{
		free(server->buffer_command);
	}

	if(server->socket != -1)
	{
		socketclose(server->socket);
	}
}

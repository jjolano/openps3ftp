#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "client.h"
#include "command.h"

struct ClientNode
{
	int data;
	struct Client* client;

	int height;
	struct ClientNode* left;
	struct ClientNode* right;
};

struct ServerClients
{
	struct ClientNode* root;
};

struct Server
{
	struct Command* command_ptr;

	bool running;
	bool should_stop;

	unsigned short port;

	int socket;

	struct pollfd* pollfds;
	nfds_t nfds;

	struct ServerClients clients;
	size_t num_clients;

	char* buffer_control;
	char* buffer_data;
	char* buffer_command;
};

/* internal server functions */
void server_pollfds_add(struct Server* server, int fd, short events);
void server_pollfds_remove(struct Server* server, int fd);
void server_client_add(struct Server* server, int fd, struct Client** client_ptr);
void server_client_find(struct Server* server, int fd, struct Client** client_ptr);
void server_client_remove(struct Server* server, int fd);

/* user application functions */
void server_init(struct Server* server, struct Command* command_ptr, unsigned short port);
uint32_t server_run(struct Server* server);
void server_stop(struct Server* server);
void server_free(struct Server* server);

#ifdef __cplusplus
}
#endif

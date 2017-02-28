/* Header file for exported PRX functions. */

#ifndef _OPENPS3FTP_H_
#define _OPENPS3FTP_H_

#ifndef _OPENPS3FTP_STRUCTS_
#include <stdbool.h>
#include <sys/poll.h>

struct Client;

typedef bool (*data_callback)(struct Client*);
typedef void (*command_callback)(struct Client*, const char[32], const char*);
typedef void (*connect_callback)(struct Client*);
typedef void (*disconnect_callback)(struct Client*);

struct CommandCallback
{
	char name[32];
	command_callback callback;
};

struct ConnectCallback
{
	connect_callback callback;
};

struct DisconnectCallback
{
	disconnect_callback callback;
};

struct Command
{
	struct CommandCallback* command_callbacks;
	int num_command_callbacks;

	struct ConnectCallback* connect_callbacks;
	int num_connect_callbacks;

	struct DisconnectCallback* disconnect_callbacks;
	int num_disconnect_callbacks;
};

struct ServerClients
{
	int socket;
	struct Client* client;
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

	struct ServerClients* clients;
	size_t num_clients;

	char* buffer_control;
	char* buffer_data;
	char* buffer_command;
};

struct ClientVariable
{
	char name[32];
	void* ptr;
};

struct Client
{
	struct Server* server_ptr;

	struct ClientVariable* cvar;
	int cvar_count;

	int socket_control;
	int socket_data;
	int socket_pasv;

	data_callback cb_data;
	char lastcmd[32];
};

/* cvar functions */
void* client_get_cvar(struct Client* client, const char name[32]);
void client_set_cvar(struct Client* client, const char name[32], void* ptr);

/* control socket functions */
void client_send_message(struct Client* client, const char* message);
void client_send_code(struct Client* client, int code, const char* message);
void client_send_multicode(struct Client* client, int code, const char* message);
void client_send_multimessage(struct Client* client, const char* message);
#endif

void ext_command_register_connect(connect_callback callback);
void ext_command_register_disconnect(disconnect_callback callback);
void ext_command_register(const char name[32], command_callback callback);

#endif

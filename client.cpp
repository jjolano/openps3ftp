#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <cstdio>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <net/poll.h>
#include <sys/file.h>

#include "const.h"
#include "server.h"
#include "client.h"
#include "command.h"
#include "common.h"

using namespace std;

void socket_send_message(int socket, string message)
{
	message += '\r';
	message += '\n';

	send(socket, message.c_str(), message.size(), 0);
}

Client::Client(int client, vector<pollfd>* pfds, map<int, Client*>* clnts, map<int, Client*>* cdata)
{
	socket_ctrl = client;
	socket_data = -1;
	socket_pasv = -1;

	buffer = new char[CMD_BUFFER];
	buffer_data = new char[DATA_BUFFER];

	pollfds = pfds;
	clients = clnts;
	clients_data = cdata;

	// cvars
	cvar_auth = false;
	cvar_rest = 0;
	cvar_fd = -1;
	cvar_use_aio = AIO_ENABLED;
}

Client::~Client(void)
{
	data_end();
	close(socket_ctrl);

	delete[] buffer;
	delete[] buffer_data;
}

void Client::send_string(string message)
{
	socket_send_message(socket_ctrl, message);
}

void Client::send_code(int code, string message)
{
	ostringstream code_str;
	code_str << code;

	socket_send_message(socket_ctrl, code_str.str() + ' ' + message);
}

void Client::send_multicode(int code, string message)
{
	ostringstream code_str;
	code_str << code;

	socket_send_message(socket_ctrl, code_str.str() + '-' + message);
}

void Client::handle_command(map<string, cmdfunc>* cmds, string cmd, string params)
{
	map<string, cmdfunc>::iterator cmds_it;
	cmds_it = (*cmds).find(cmd);

	if(cmds_it != (*cmds).end())
	{
		// command handler found
		(cmds_it->second)(this, params);
	}
	else
	{
		// no handler found
		send_code(502, cmd + " not supported");
	}

	lastcmd = cmd;
}

void Client::handle_data(void)
{
	int ret;
	ret = data_handler(this);

	if(ret != 0)
	{
		data_end();
	}
}

int Client::data_start(func f, short events)
{
	if(socket_data == -1)
	{
		if(socket_pasv == -1)
		{
			// active mode
			sockaddr_in sa;
			socklen_t len = sizeof(sa);

			getpeername(socket_ctrl, (sockaddr*)&sa, &len);
			sa.sin_port = htons(20);

			int socket_data_new;
			socket_data_new = socket(PF_INET, SOCK_STREAM, 0);

			if(connect(socket_data_new, (sockaddr*)&sa, len) == -1)
			{
				close(socket_data_new);
				return socket_data;
			}

			socket_data = socket_data_new;
		}
		else
		{
			// passive mode
			socket_data = accept(socket_pasv, NULL, NULL);

			close(socket_pasv);
			socket_pasv = -1;
		}
	}

	if(socket_data != -1)
	{
		// add to pollfds
		pollfd data_pollfd;
		data_pollfd.fd = socket_data;
		data_pollfd.events = events | POLLIN;

		pollfds->push_back(data_pollfd);

		// register socket
		clients_data->insert(make_pair(socket_data, this));

		data_handler = f;
	}

	return socket_data;
}

void Client::data_end(void)
{
	if(socket_data != -1)
	{
		for(vector<pollfd>::iterator it = pollfds->begin(); it != pollfds->end(); it++)
		{
			if(it->fd == socket_data)
			{
				pollfds->erase(it);
				break;
			}
		}

		clients_data->erase(clients_data->find(socket_data));
	}

	close(socket_data);
	close(socket_pasv);

	socket_data = -1;
	socket_pasv = -1;

	data_handler = NULL;

	// cvars
	cvar_fd = -1;
}

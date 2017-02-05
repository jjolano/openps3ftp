#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <cstdio>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#ifndef _PS3SDK_
#include <net/poll.h>
#include <sys/file.h>
#else
#include <sys/poll.h>
#include <cell/cell_fs.h>
#endif

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

Client::Client(int sock, vector<pollfd>* pfds, map<int, Client*>* cdata)
{
	socket_ctrl = sock;
	socket_data = -1;
	socket_pasv = -1;

	buffer = new (nothrow) char[CMD_BUFFER];
	buffer_data = new (nothrow) char[DATA_BUFFER];
	
	#if defined(_USE_IOBUFFERS_) || defined(_PS3SDK_)
	sysMemContainerCreate(&buffer_io, IO_BUFFER);
	#endif

	pollfds = pfds;
	clients_data = cdata;

	// cvars
	cvar_auth = false;
	cvar_rest = 0;
	cvar_fd = -1;
	cvar_aio.fd = -1;
	cvar_aio_id = -1;
}

Client::~Client(void)
{
	data_end();

	#ifdef _USE_LINGER_
	shutdown(socket_ctrl, SHUT_RDWR);
	#endif

	close(socket_ctrl);

	delete[] buffer;
	delete[] buffer_data;

	#if defined(_USE_IOBUFFERS_) || defined(_PS3SDK_)
	sysMemContainerDestroy(buffer_io);
	#endif
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

void Client::handle_command(map<string, cmd_callback>* cmds, string cmd, string params)
{
	map<string, cmd_callback>::iterator cmds_it = (*cmds).find(cmd);

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
	int ret = data_handler(this);

	if(ret != 0)
	{
		data_end();
	}
}

int Client::data_start(data_callback callback, short events)
{
	if(socket_data == -1)
	{
		if(socket_pasv == -1)
		{
			// legacy active mode
			sockaddr_in sa;
			socklen_t len = sizeof(sa);

			getpeername(socket_ctrl, (sockaddr*)&sa, &len);
			sa.sin_port = htons(20);

			socket_data = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			// set socket option
			int optval = DATA_BUFFER;
			setsockopt(socket_data, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
			setsockopt(socket_data, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));

			#ifdef _USE_LINGER_
			linger opt_linger;
			opt_linger.l_onoff = 1;
			opt_linger.l_linger = 0;
			setsockopt(socket_data, SOL_SOCKET, SO_LINGER, &opt_linger, sizeof(opt_linger));
			#endif

			if(connect(socket_data, (sockaddr*)&sa, len) == -1)
			{
				#ifdef _USE_LINGER_
				shutdown(socket_data, SHUT_RDWR);
				#endif

				close(socket_data);
				socket_data = -1;
			}
		}
		else
		{
			// passive mode
			socket_data = accept(socket_pasv, NULL, NULL);

			// set socket option
			int optval = DATA_BUFFER;
			setsockopt(socket_data, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
			setsockopt(socket_data, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));

			#ifdef _USE_LINGER_
			linger opt_linger;
			opt_linger.l_onoff = 1;
			opt_linger.l_linger = 0;
			setsockopt(socket_data, SOL_SOCKET, SO_LINGER, &opt_linger, sizeof(opt_linger));
			#endif

			#ifdef _USE_LINGER_
			shutdown(socket_pasv, SHUT_RDWR);
			#endif

			close(socket_pasv);
			socket_pasv = -1;
		}
	}

	if(socket_data != -1)
	{
		// make sure we have a buffer allocated
		if(!buffer_data)
		{
			buffer_data = new (nothrow) char[DATA_BUFFER];

			if(!buffer_data)
			{
				// rip
				send_code(500, "Failed to allocate memory! Try restarting the app.");

				#ifdef _USE_LINGER_
				shutdown(socket_data, SHUT_RDWR);
				#endif

				close(socket_data);
				return -1;
			}
		}

		// add to pollfds
		pollfd data_pollfd;
		data_pollfd.fd = FD(socket_data);
		data_pollfd.events = (events|POLLIN|POLLPRI);

		pollfds->push_back(data_pollfd);

		// register socket
		clients_data->insert(make_pair(socket_data, this));

		// register data handler
		data_handler = callback;
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

		#ifdef _USE_LINGER_
		shutdown(socket_data, SHUT_RDWR);
		#endif

		close(socket_data);
		socket_data = -1;
	}

	if(socket_pasv != -1)
	{
		#ifdef _USE_LINGER_
		shutdown(socket_pasv, SHUT_RDWR);
		#endif

		close(socket_pasv);
		socket_pasv = -1;
	}

	data_handler = NULL;

	// cvars
	cvar_fd = -1;
	cvar_aio.fd = -1;
}

void Client::set_io_buffer(s32 fd)
{
	#if defined(_USE_IOBUFFERS_) || defined(_PS3SDK_)
	sysFsSetIoBuffer(fd, DATA_BUFFER, SYS_FS_IO_BUFFER_PAGE_SIZE_64KB, buffer_io);
	#endif
}

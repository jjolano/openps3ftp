#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <sstream>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <sys/memory.h>

#ifndef _PS3SDK_
#include <net/net.h>
#include <net/poll.h>
#include <sys/thread.h>
#include <sys/lv2errno.h>
#else
#include <netex/net.h>
#include <sys/poll.h>
#include <sys/ppu_thread.h>
#include <sys/syscall.h>
#endif

#include "const.h"
#include "server.h"
#include "client.h"
#include "command.h"
#include "common.h"
#include "util.h"

using namespace std;

int clean_tmp(void)
{
	s32 fd_dir;
	if(sysLv2FsOpenDir(TMP_DIR, &fd_dir) == 0)
	{
		sysFSDirent dirent;
		u64 read;
		
		while(sysLv2FsReadDir(fd_dir, &dirent, &read) == 0 && read != 0)
		{
			if(strcmp(dirent.d_name, ".") == 0
			|| strcmp(dirent.d_name, "..") == 0)
			{
				continue;
			}

			// delete files
			stringstream filename;
			filename << TMP_DIR;
			filename << '/' << dirent.d_name;

			sysLv2FsUnlink(filename.str().c_str());
		}

		sysLv2FsCloseDir(fd_dir);
		return 0;
	}

	return 1;
}

void server_start(void* arg)
{
	app_status* status = (app_status*)arg;

	// Create server variables.
	vector<pollfd> pollfds;				// stores all socket connection info
	map<int, Client*> clients;			// stores all client info, indexed by the ftp control socket
	map<int, Client*> clients_data;		// stores all client info, indexed by the ftp data socket
	map<string, cmd_callback> commands;	// stores all registered ftp commands

	bool aio_toggle = false;			// the default state of async io
	string tmp_dir;						// the tmp directory location for the ftp server

	// Register server commands.
	register_cmds(&commands);

	// Attempt to create/use existing temporary directory.
	if(clean_tmp() == 0 || sysLv2FsMkdir(TMP_DIR, 0777) == 0)
	{
		tmp_dir = TMP_DIR;
	}

	// Create server socket.
	int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Set socket options.
	int optval = 1;
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	setsockopt(server, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_port = htons(21);
	sa.sin_addr.s_addr = INADDR_ANY;

	// Bind port 21 to server socket.
	if(bind(server, (sockaddr*)&sa, sizeof(sa)) != 0)
	{
		// Could not bind port to socket.
		status->is_running = 0;
		close(server);
		sysThreadExit(1);
		return;
	}

	// Start listening for connections.
	listen(server, LISTEN_BACKLOG);

	// Create and add server pollfd.
	pollfd server_pollfd;
	server_pollfd.fd = FD(server);
	server_pollfd.events = POLLIN;

	pollfds.push_back(server_pollfd);

	// Server loop.
	while(status->is_running)
	{
		int p = poll(&pollfds[0], (nfds_t)pollfds.size(), 500);

		if(p == 0)
		{
			// nothing happened
			continue;
		}

		if(p == -1)
		{
			// poll error
			status->is_running = 0;
			break;
		}

		// new event detected!
		// iterate through connected sockets
		for(vector<pollfd>::iterator pfd_it = pollfds.begin(); pfd_it != pollfds.end(); pfd_it++)
		{
			if(p == 0)
			{
				break;
			}

			pollfd pfd = *pfd_it;

			if(pfd.revents != 0)
			{
				p--;

				// check if valid socket
				if(pfd.revents & POLLNVAL)
				{
					pollfds.erase(pfd_it);
					continue;
				}

				// handle socket events, depending on socket type
				// server
				if(OFD(pfd.fd) == server)
				{
					// incoming connection
					int client_new = accept(server, NULL, NULL);

					if(client_new == -1)
					{
						continue;
					}

					#ifdef _USE_LINGER_
					linger opt_linger;
					opt_linger.l_onoff = 1;
					opt_linger.l_linger = 0;
					setsockopt(client_new, SOL_SOCKET, SO_LINGER, &opt_linger, sizeof(opt_linger));
					#endif

					// create new internal client
					Client* client = new (nothrow) Client(client_new, &pollfds, &clients_data);

					// check if allocated successfully
					if(!client)
					{
						#ifdef _USE_LINGER_
						shutdown(client_new, SHUT_RDWR);
						#endif

						close(client_new);
						continue;
					}

					// set default variables
					client->cvar_use_aio = aio_toggle;
					client->cvar_fd_tempdir = tmp_dir;
					
					optval = CMD_BUFFER;
					setsockopt(client_new, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
					setsockopt(client_new, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval));

					optval = 1;
					setsockopt(client_new, SOL_SOCKET, SO_OOBINLINE, &optval, sizeof(optval));

					// assign socket to internal client
					clients.insert(make_pair(client_new, client));

					// create and add pollfd
					pollfd client_pollfd;
					client_pollfd.fd = FD(client_new);
					client_pollfd.events = (POLLIN|POLLRDNORM|POLLRDBAND|POLLPRI);

					pollfds.push_back(client_pollfd);

					// hello!
					client->send_multicode(220, WELCOME_MSG);
					client->send_code(220, "Ready.");
					break;
				}
				else
				{
					// check if socket is data
					map<int, Client*>::iterator cdata_it = clients_data.find(OFD(pfd.fd));

					if(cdata_it != clients_data.end())
					{
						// get client
						Client* client = cdata_it->second;

						// check for disconnection
						if(pfd.revents & (POLLHUP|POLLERR))
						{
							client->data_end();
							continue;
						}

						// execute data handler
						if(pfd.revents & (POLLOUT|POLLWRNORM|POLLIN|POLLRDNORM|POLLRDBAND|POLLPRI))
						{
							client->handle_data();
						}

						continue;
					}

					// check if socket is a client
					map<int, Client*>::iterator client_it = clients.find(OFD(pfd.fd));

					if(client_it != clients.end())
					{
						// get client
						Client* client = client_it->second;

						// check disconnect event
						if(pfd.revents & (POLLHUP|POLLERR))
						{
							delete client;
							pollfds.erase(pfd_it);
							clients.erase(client_it);
							continue;
						}

						// check receiving event
						if(pfd.revents & (POLLIN|POLLRDNORM|POLLRDBAND|POLLPRI))
						{
							// make sure we have a buffer allocated
							if(!client->buffer)
							{
								client->buffer = new (nothrow) char[CMD_BUFFER];

								if(!client->buffer)
								{
									// rip
									client->send_code(500, "Failed to allocate memory! Try restarting the app.");
									continue;
								}
							}

							ssize_t bytes = recv(client->socket_ctrl, client->buffer, CMD_BUFFER - 1, 0);

							// check if recv was valid
							if(bytes < 2)
							{
								// socket invalid, dropped, or malformed data
								delete client;
								pollfds.erase(pfd_it);
								clients.erase(client_it);
								continue;
							}

							client->buffer[bytes - 2] = '\0';

							// handle commands at a basic level
							string data(client->buffer);

							// pretend we didn't see a blank line
							if(data.empty())
							{
								continue;
							}

							stringstream in(data);

							string cmd, params, param;
							in >> cmd;

							while(in >> param)
							{
								if(!params.empty())
								{
									params += ' ';
								}

								params += param;
							}

							// capitalize command string internally
							cmd = string_to_upper(cmd);

							// internal commands
							if(cmd == "QUIT")
							{
								client->send_code(221, "Bye");
								
								delete client;
								pollfds.erase(pfd_it);
								clients.erase(client_it);
								continue;
							}

							if(cmd == "APP_EXIT")
							{
								client->send_code(221, "Exiting...");

								status->is_running = 0;
								break;
							}

							if(cmd == "AIO")
							{
								aio_toggle = !aio_toggle;
								string aio_status(aio_toggle ? "enabled" : "disabled");

								client->send_code(200, "Asynchronous IO " + aio_status);
								continue;
							}

							// handle client command
							client->handle_command(&commands, cmd, params);
						}

						continue;
					}
				}
			}
		}

		// update status
		status->num_clients = clients.size();
		status->num_connections = pollfds.size() - 1;
	}

	// Close sockets.
	for(map<int, Client*>::iterator client_it = clients.begin(); client_it != clients.end(); client_it++)
	{
		Client* client = client_it->second;
		delete client;
	}

	// Remove temp folder.
	if(!tmp_dir.empty())
	{
		clean_tmp();
		sysLv2FsRmdir(tmp_dir.c_str());
	}

	status->is_running = 0;

	close(server);
	sysThreadExit(0);
}

void server_start_ex(uint64_t arg)
{
	server_start((void*)arg);
}

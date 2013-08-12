/*
 * ftp.cpp: main server and client socket handling
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jjolano@me.com> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return John Olano
 * ----------------------------------------------------------------------------
 */

#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

#include <NoRSX.h>
#include <sys/thread.h>
#include <net/net.h>
#include <netinet/in.h>
#include <netdb.h>

#include "ftp.h"
#include "defs.h"

using namespace std;

vector<pollfd> pfd;
map<int,ftp_client> client;
map<int,int> data_control;
map<string,cmdhnd> command;

void event_client_drop(ftp_client* clnt);

void ftpTerminate()
{
	for(map<int,ftp_client>::iterator cit = client.begin(); cit != client.end(); cit++)
	{
		event_client_drop(&(cit->second));
		(cit->second).data_close();
	}

	for(vector<pollfd>::iterator it = pfd.begin(); it != pfd.end(); it++)
	{
		closesocket(it->fd);
	}

	pfd.clear();
	client.clear();
	data_control.clear();
	command.clear();
}

void register_cmd(string cmd, cmdhnd handler)
{
	command[cmd] = handler;
}

void register_cmds();

void ftp_client::control_sendCustom(string message)
{
	// append proper line ending
	message += '\r';
	message += '\n';

	send(sock_control, message.c_str(), message.size(), 0);
}

void ftp_client::control_sendCode(unsigned int code, string message, bool multi)
{
	// format string
	ostringstream out;
	out << code << (multi ? '-' : ' ');

	// send to control socket
	control_sendCustom(out.str() + message);
}

bool ftp_client::data_open(datahnd handler, short events)
{
	// reset data state
	data_handler = NULL;

	if(sock_data == -1)
	{
		if(sock_pasv == -1)
		{
			// legacy PORT 20 connection
			sockaddr_in sa;
			socklen_t len = sizeof sa;
			
			getpeername(sock_control, (sockaddr*)&sa, &len);
			sa.sin_port = htons(20);

			sock_data = socket(PF_INET, SOCK_STREAM, 0);

			if(connect(sock_data, (sockaddr*)&sa, len) == -1)
			{
				// connect failed
				closesocket(sock_data);
				sock_data = -1;
				return false;
			}
		}
		else
		{
			// passive mode accept connection
			sockaddr sa;
			socklen_t len = sizeof sa;

			sock_data = accept(sock_pasv, &sa, &len);

			closesocket(sock_pasv);
			sock_pasv = -1;

			if(sock_data == -1)
			{
				// accept failed
				return false;
			}
		}
	}

	// register data handler
	data_handler = handler;

	// register pollfd for data connection
	pollfd data_pfd;
	data_pfd.fd = sock_data;
	data_pfd.events = events;
	pfd.push_back(data_pfd);

	// reference
	data_control[sock_data] = sock_control;
	return true;
}

int ftp_client::data_send(char* data, int bytes)
{
	if(sock_data == -1)
	{
		return -1;
	}

	return send(sock_data, data, bytes, 0);
}

int ftp_client::data_recv(char* data, int bytes)
{
	if(sock_data == -1)
	{
		return -1;
	}

	return recv(sock_data, data, bytes, MSG_WAITALL);
}

void ftp_client::data_close()
{
	if(sock_data != -1)
	{
		// remove from pollfd
		for(vector<pollfd>::iterator it = pfd.begin(); it != pfd.end(); it++)
		{
			if(it->fd == sock_data)
			{
				pfd.erase(it);
			}
		}

		// remove references and close socket
		data_handler = NULL;
		data_control.erase(sock_data);
		closesocket(sock_data);
		sock_data = -1;
	}

	if(sock_pasv != -1)
	{
		// close listener
		closesocket(sock_pasv);
		sock_pasv = -1;
	}
}

void ftpInitialize(void* arg)
{
	// Obtain graphics pointer
	NoRSX* GFX = static_cast<NoRSX*>(arg);
	
	// Create server socket
	int sock_listen = socket(PF_INET, SOCK_STREAM, 0);
	
	sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(21);
	myaddr.sin_addr.s_addr = INADDR_ANY;

	if(bind(sock_listen, (struct sockaddr*)&myaddr, sizeof myaddr) == -1)
	{
		// 0x1337BEEF: Socket bind error
		closesocket(sock_listen);
		GFX->AppExit();
		sysThreadExit(0x1337BEEF);
	}

	listen(sock_listen, LISTEN_BACKLOG);

	pollfd listen_pfd;
	listen_pfd.fd = sock_listen;
	listen_pfd.events = POLLIN;
	pfd.push_back(listen_pfd);

	// Register FTP command handlers
	register_cmds();

	// Allocate memory for commands
	char* data = new char[CMDBUFFER];

	// Main thread loop
	while(GFX->GetAppStatus() != APP_EXIT)
	{
		int p = poll(&pfd[0], (nfds_t)pfd.size(), 1000);

		if(p == -1)
		{
			// 0x13371010: Socket poll error
			delete [] data;
			ftpTerminate();
			GFX->AppExit();
			sysThreadExit(0x13371010);
		}

		// Loop if poll > 0
		for(unsigned int i = 0; (p > 0 && i < pfd.size()); i++)
		{
			if(pfd[i].revents == 0)
			{
				continue;
			}

			p--;

			int sock = pfd[i].fd;

			// Listener socket event
			if(sock == sock_listen)
			{
				if(pfd[i].revents & POLLIN)
				{
					// accept new connection
					sockaddr sa;
					socklen_t len = sizeof sa;
					int nfd = accept(sock, &sa, &len);

					// add to pollfds
					pollfd new_pfd;
					new_pfd.fd = nfd;
					new_pfd.events = FTP_DATA_EVENT_RECV;
					pfd.push_back(new_pfd);

					// add to clients
					ftp_client clnt;
					clnt.sock_control = nfd;
					clnt.sock_data = -1;
					clnt.sock_pasv = -1;
					clnt.data_handler = NULL;
					clnt.active = true;
					client[nfd] = clnt;

					// welcome
					clnt.control_sendCode(220, APP_NAME " version " APP_VERSION, true);
					clnt.control_sendCode(220, "by " APP_AUTHOR, false);
					continue;
				}
				else
				{
					// 0x1337ABCD: some error happened to the server
					delete [] data;
					ftpTerminate();
					GFX->AppExit();
					sysThreadExit(0x1337ABCD);
				}
			}

			// get client info
			map<int,ftp_client>::iterator it;
			ftp_client* clnt;

			it = client.find(sock);

			if(it != client.end())
			{
				clnt = &(it->second);
			}
			else
			{
				clnt = &client[data_control[sock]];
			}

			// Disconnect event
			if((pfd[i].revents & (POLLNVAL|POLLHUP|POLLERR)) || !clnt->active)
			{
				// socket disconnected
				if(it != client.end())
				{
					// client dropped
					event_client_drop(clnt);
					closesocket(sock);
					clnt->data_close();
					client.erase(sock);
				}
				else
				{
					// data connection dropped
					clnt->data_close();
				}

				pfd.erase(pfd.begin() + i);
				
				continue;
			}

			// Sending data event
			if(pfd[i].revents & FTP_DATA_EVENT_SEND)
			{
				// call data handler
				(*clnt->data_handler)(sock);
				continue;
			}

			// Receiving data event
			if(pfd[i].revents & FTP_DATA_EVENT_RECV)
			{
				if(it != client.end())
				{
					// Control socket
					int bytes = recv(sock, data, CMDBUFFER - 1, 0);

					if(bytes <= 0)
					{
						// invalid, drop client
						event_client_drop(clnt);
						closesocket(sock);
						clnt->data_close();
						client.erase(sock);
						pfd.erase(pfd.begin() + i);
					}

					if(bytes <= 2)
					{
						// invalid command, continue
						continue;
					}

					// handle command
					string cmdstr(data);
					cmdstr.resize(bytes - 2);

					string::size_type pos = cmdstr.find(' ', 0);

					string cmd = cmdstr.substr(0, pos);
					string args;

					if(pos != string::npos)
					{
						args = cmdstr.substr(pos + 1);
					}

					transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

					// find command handler
					map<string,cmdhnd>::iterator it = command.find(cmd);

					if(it != command.end())
					{
						// call handler
						(it->second)(clnt, cmd, args);
					}
					else
					{
						// command not found
						clnt->control_sendCode(502, "Command not implemented", false);
					}
				}
				else
				{
					// Data socket
					// call data handler
					(*clnt->data_handler)(sock);
				}

				continue;
			}
		}
	}

	delete [] data;
	ftpTerminate();
	sysThreadExit(0);
}

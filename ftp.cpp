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
#include <utility>
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

#define FD(socket)			((socket)&~SOCKET_FD_MASK)

using namespace std;

typedef map<int,int> ftp_drefs;
typedef map<int,ftp_client> ftp_clnts;

vector<pollfd> pfd;
ftp_drefs datarefs;

nfds_t nfds;

void event_client_drop(ftp_client* clnt);
void register_cmds(ftp_chnds* command);
void closedata(ftp_client* clnt);

// Registers an FTP command to a function
//void register_cmd(string cmd, cmdhnd handler)
//{
//	command.insert(make_pair(cmd, handler));
//}

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
	out << code;

	string cmsg = out.str() + (multi ? '-' : ' ') + message;

	// send to control socket
	control_sendCustom(cmsg);
}

void ftp_client::control_sendCode(unsigned int code, string message)
{
	control_sendCode(code, message, false);
}

bool ftp_client::data_open(datahnd handler, short events)
{
	// try to get data connection
	if(sock_data == -1)
	{
		if(sock_pasv == -1)
		{
			// legacy PORT 20 connection
			sockaddr_in sa;
			socklen_t len = sizeof(sa);
			
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
			sock_data = accept(sock_pasv, NULL, NULL);

			closesocket(sock_pasv);
			sock_pasv = -1;

			if(sock_data == -1)
			{
				// accept failed
				return false;
			}
		}
	}

	// register pollfd for data connection
	pollfd data_pfd;
	data_pfd.fd = FD(sock_data);
	data_pfd.events = events | POLLIN;
	pfd.push_back(data_pfd);
	nfds++;

	// reference
	datarefs[sock_data] = sock_control;
	data_handler = handler;
	return true;
}

void ftp_client::data_close()
{
	if(sock_data != -1)
	{
		// close socket
		closesocket(sock_data);

		if(datarefs.find(sock_data) != datarefs.end())
		{
			datarefs.erase(sock_data);
		}

		sock_data = -1;
	}
}

void ftpInitialize(void* arg)
{
	// Obtain graphics pointer
	NoRSX* GFX = static_cast<NoRSX*>(arg);

	// Allocate memory for commands
	char* data = new char[CMD_BUFFER];

	if(data == NULL)
	{
		GFX->AppExit();
		sysThreadExit(1);
	}

	// Create server socket
	int sock_listen = socket(PF_INET, SOCK_STREAM, 0);
	
	sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(21);
	myaddr.sin_addr.s_addr = INADDR_ANY;

	if(bind(sock_listen, (struct sockaddr*)&myaddr, sizeof myaddr) == -1)
	{
		GFX->AppExit();
		closesocket(sock_listen);
		sysThreadExit(2);
	}

	listen(sock_listen, LISTEN_BACKLOG);

	// Register FTP command handlers
	ftp_chnds command;
	register_cmds(&command);

	// Add server to poll
	pollfd listen_pfd;
	listen_pfd.fd = FD(sock_listen);
	listen_pfd.events = POLLIN;
	pfd.push_back(listen_pfd);

	u64 ret_val = 0;
	nfds = 1;

	ftp_clnts client;

	// Main thread loop
	while(GFX->GetAppStatus())
	{
		// apparently libnet's poll is super inefficient, it slows down
		// threefold. just gonna use the syscall directly.
		int p = netPoll(&pfd[0], nfds, 250);

		if(p == -1)
		{
			GFX->AppExit();
			ret_val = 3;
			break;
		}

		if(p == 0)
		{
			continue;
		}

		// handle listener socket events
		if(pfd[0].revents != 0)
		{
			p--;
		}

		// handle client and data sockets
		u16 i;
		for(i = 1; (p > 0 && i < nfds); i++)
		{
			if(pfd[i].revents == 0)
			{
				continue;
			}

			p--;

			int sock_fd = (pfd[i].fd | SOCKET_FD_MASK);

			// get client info
			ftp_drefs::iterator it = datarefs.find(sock_fd);
			bool isData = (it != datarefs.end());
			ftp_client* clnt = &(client[isData ? it->second : sock_fd]);

			// Disconnect event
			if(pfd[i].revents & (POLLNVAL|POLLHUP|POLLERR))
			{
				if(!isData)
				{
					// client dropped
					closesocket(sock_fd);
					event_client_drop(clnt);
					client.erase(sock_fd);
				}
				else
				{
					// data connection
					closedata(clnt);
				}

				pfd.erase(pfd.begin() + i);
				nfds--;
				i--;
				continue;
			}

			// Sending data event
			if(pfd[i].revents & DATA_EVENT_SEND)
			{
				// call data handler
				(clnt->data_handler)(sock_fd);

				if(clnt->sock_data == -1)
				{
					pfd.erase(pfd.begin() + i);
					nfds--;
					i--;
				}
				continue;
			}

			// Receiving data event
			if(pfd[i].revents & DATA_EVENT_RECV)
			{
				if(!isData)
				{
					// Control socket
					int bytes = recv(sock_fd, data, CMD_BUFFER, 0);

					if(bytes <= 0)
					{
						// invalid, drop client
						closesocket(sock_fd);
						event_client_drop(clnt);
						client.erase(sock_fd);
						
						pfd.erase(pfd.begin() + i);
						nfds--;
						i--;
						continue;
					}

					if(bytes <= 2)
					{
						// invalid command, continue
						clnt->control_sendCode(500, "Invalid command");
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

					// handle quit internally
					if(cmd == "QUIT")
					{
						clnt->control_sendCode(221, "Goodbye");

						closesocket(sock_fd);
						event_client_drop(clnt);
						client.erase(sock_fd);

						pfd.erase(pfd.begin() + i);
						nfds--;
						i--;
						continue;
					}
					
					if(cmd == "EXIT")
					{
						GFX->AppExit();
						break;
					}

					// find command handler
					ftp_chnds::iterator cit = command.find(cmd);

					if(cit != command.end())
					{
						// call handler
						(cit->second)(clnt, cmd, args);
					}
					else
					{
						// command not found
						clnt->control_sendCode(502, cmd + " not implemented");
					}
				}
				else
				{
					// Data socket

					// check drop
					int bytes = recv(sock_fd, data, 1, MSG_PEEK);

					if(bytes <= 0)
					{
						closedata(clnt);
						pfd.erase(pfd.begin() + i);
						nfds--;
						i--;
						continue;
					}

					// call data handler
					(clnt->data_handler)(sock_fd);

					if(clnt->sock_data == -1)
					{
						pfd.erase(pfd.begin() + i);
						nfds--;
						i--;
					}
				}

				continue;
			}
		}

		if(pfd[0].revents != 0)
		{
			// accept new connection
			int nfd = accept(sock_listen, NULL, NULL);

			if(nfd == -1)
			{
				continue;
			}

			// add to pollfds
			pollfd new_pfd;
			new_pfd.fd = FD(nfd);
			new_pfd.events = DATA_EVENT_RECV;
			pfd.push_back(new_pfd);
			nfds++;

			// add to clients
			ftp_client nc;

			nc.sock_control = nfd;
			nc.sock_data = -1;
			nc.sock_pasv = -1;

			client[nfd] = nc;

			// welcome
			nc.control_sendCode(220, APP_NAME " v" APP_VERSION);
		}
	}

	closesocket(sock_listen);

	ftp_clnts::iterator cit;
	for(cit = client.begin(); cit != client.end(); cit++)
	{
		ftp_client* clnt = &(cit->second);

		clnt->control_sendCode(421, "Server is shutting down");
		closesocket(clnt->sock_control);
		event_client_drop(clnt);
	}

	delete [] data;

	datarefs.clear();
	client.clear();
	command.clear();
	pfd.clear();

	sysThreadExit(ret_val);
}

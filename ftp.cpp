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
typedef map<string,cmdhnd> ftp_chnds;
typedef map<int,void (*)(ftp_client* clnt, char* buffer)> ftp_dhnds;
typedef map<int,ftp_client> ftp_clnts;
typedef vector<pollfd> ftp_socks;

ftp_socks pfd;
ftp_chnds command;
ftp_drefs datarefs;
ftp_dhnds datafunc;
ftp_clnts client;

nfds_t nfds;

void event_client_drop(ftp_client* clnt);
void register_cmds();

// Terminates FTP server and all connections
void ftpTerminate()
{
	for(ftp_clnts::iterator cit = client.begin(); cit != client.end(); cit++)
	{
		ftp_client* clnt = &(cit->second);
		clnt->control_sendCode(421, "Server is shutting down");
		event_client_drop(clnt);
	}

	u16 i;
	for(i = 0; i < nfds; i++)
	{
		closesocket(pfd[i].fd);
	}

	pfd.clear();
	client.clear();
	datafunc.clear();
	datarefs.clear();
	command.clear();
}

// Registers an FTP command to a function
void register_cmd(string cmd, cmdhnd handler)
{
	command.insert(make_pair(cmd, handler));
}

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

bool ftp_client::data_open(void (*handler)(ftp_client* clnt, char* buffer), short events)
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
	data_pfd.events = events | POLLERR | POLLHUP | POLLNVAL;
	pfd.push_back(data_pfd);
	nfds++;

	// reference
	datarefs[sock_data] = sock_control;
	datafunc[sock_data] = handler;
	return true;
}

void ftp_client::data_close()
{
	if(sock_data != -1)
	{
		// remove from pollfd
		u16 i;
		for(i = 1; i < nfds; i++)
		{
			if(pfd[i].fd == FD(sock_data))
			{
				pfd.erase(pfd.begin() + i);
				nfds--;
				break;
			}
		}

		// remove references
		datafunc.erase(sock_data);
		datarefs.erase(sock_data);

		// close socket
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

	// Allocate memory for commands
	char* data = new char[CMD_BUFFER];

	if(data == NULL)
	{
		GFX->AppExit();
		sysThreadExit(1);
	}

	char* buffer = new char[DATA_BUFFER];

	if(buffer == NULL)
	{
		GFX->AppExit();
		sysThreadExit(2);
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
		sysThreadExit(3);
	}

	listen(sock_listen, LISTEN_BACKLOG);

	// Register FTP command handlers
	register_cmds();

	// Add server to poll
	pollfd listen_pfd;
	listen_pfd.fd = FD(sock_listen);
	listen_pfd.events = POLLIN;
	pfd.push_back(listen_pfd);

	u64 ret_val = 0;
	nfds = 1;

	// Main thread loop
	while(GFX->GetAppStatus())
	{
		// apparently libnet's poll is super inefficient, it slows down
		// threefold. just gonna use the syscall directly.
		static int p;
		p = netPoll(&pfd[0], nfds, 250);

		if(p < 0)
		{
			GFX->AppExit();
			ret_val = 4;
			break;
		}

		// handle listener socket events
		if(p > 0 && pfd[0].revents > 0)
		{
			p--;

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
			client[nfd].sock_control = nfd;
			client[nfd].sock_data = -1;
			client[nfd].sock_pasv = -1;

			// welcome
			client[nfd].control_sendCode(220, APP_NAME " v" APP_VERSION);
		}

		// handle client and data sockets
		static u16 i;
		for(i = 1; (p > 0 && i < nfds); i++)
		{
			if(pfd[i].revents == 0)
			{
				continue;
			}

			p--;

			static int sock_fd;
			sock_fd = (pfd[i].fd | SOCKET_FD_MASK);

			// get client info
			static ftp_drefs::iterator it;
			static ftp_client* clnt;
			static bool isData;

			it = datarefs.find(sock_fd);
			isData = (it != datarefs.end());
			clnt = &(client[isData ? it->second : sock_fd]);

			// Disconnect event
			if(pfd[i].revents & POLLNVAL
			|| pfd[i].revents & POLLHUP
			|| pfd[i].revents & POLLERR)
			{
				closesocket(sock_fd);

				if(!isData)
				{
					// client dropped
					event_client_drop(clnt);
					client.erase(sock_fd);
				}
				else
				{
					// data connection
					datafunc.erase(sock_fd);
					datarefs.erase(sock_fd);
					clnt->sock_data = -1;
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
				(datafunc[sock_fd])(clnt, buffer);

				if(clnt->sock_data == -1)
				{
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
						event_client_drop(clnt);
						closesocket(sock_fd);
						
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
						
						event_client_drop(clnt);
						closesocket(sock_fd);

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
					// call data handler
					(datafunc[sock_fd])(clnt, buffer);

					if(clnt->sock_data == -1)
					{
						i--;
					}
				}

				continue;
			}
		}
	}

	ftpTerminate();
	delete [] data;
	delete [] buffer;
	sysThreadExit(ret_val);
}

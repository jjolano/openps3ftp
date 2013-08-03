/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jjolano@me.com> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return John Olano
 * ----------------------------------------------------------------------------
 */

#include <cstdio>
#include <map>
#include <vector>
#include <string>
#include <algorithm>

#include <NoRSX.h>
#include <sys/socket.h>
#include <sys/thread.h>
#include <net/net.h>
#include <net/poll.h>
#include <netinet/in.h>
#include <netdb.h>

#include "ftp.h"
#include "opf.h"
#include "ftpcmd.h"

#define CMDBUFFER			1024
#define LISTEN_BACKLOG		10
#define FD(socket)			((socket)&~SOCKET_FD_MASK)

using namespace std;

// poll vector
vector<pollfd> pfd;

// data connection/handler maps
map<int,datahandler> m_datahndl;
map<int,ftp_client*> m_dataclnt;

// closes a socket
void sock_close(int socket)
{
	if(socket > -1)
	{
		netClose(FD(socket));
	}
}

// closes a vector of pollfd sockets
void closeAll(vector<pollfd> &pfd)
{
	for(vector<pollfd>::iterator it = pfd.begin(); it != pfd.end(); ++it)
	{
		sock_close(it->fd);
	}
}

// sends a FTP response message to client, specify multi=true for continuation response
void ftp_client::response(unsigned int code, string message, bool multi)
{
	char *msg;
	msg = new char[CMDBUFFER];

	int bytes = snprintf(msg, CMDBUFFER, "%i%s%s\r\n", code, (multi ? "-" : " "), message.c_str());

	send(fd, msg, bytes, 0);
	delete [] msg;
}

// shortcut for a simple response
void ftp_client::response(unsigned int code, string message)
{
	response(code, message, false);
}

// send a custom ftp response to client
void ftp_client::cresponse(string message)
{
	char *msg;
	msg = new char[CMDBUFFER];

	int bytes = snprintf(msg, CMDBUFFER, "%s\r\n", message.c_str());

	send(fd, msg, bytes, 0);
	delete [] msg;
}

void register_data_handler(ftp_client* clnt, int data_fd, datahandler data_handler, int event)
{
	// add to pollfds
	pollfd data_pfd;
	data_pfd.fd = data_fd;

	switch(event)
	{
		case FTP_DATA_EVENT_SEND:
			data_pfd.events = (POLLOUT|POLLWRNORM);
			break;
		case FTP_DATA_EVENT_RECV:
			data_pfd.events = (POLLIN|POLLRDNORM);
			break;
		default:
			return;
	}

	pfd.push_back(data_pfd);

	// add handler and reference
	m_datahndl[data_fd] = data_handler;
	m_dataclnt[data_fd] = clnt;
}

void ftp_main(void *arg)
{
	// Obtain graphics pointer
	NoRSX* GFX = static_cast<NoRSX*>(arg);

	// Maps and vectors
	ftpcmd_handler m_cmd;
	map<int,ftp_client> m_clnt;

	// Create server socket
	sockaddr_in myaddr;

	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(21);
	myaddr.sin_addr.s_addr = INADDR_ANY;

	int sockfd = socket(PF_INET, SOCK_STREAM, 0);

	pollfd listen_pfd;
	listen_pfd.fd = sockfd;
	listen_pfd.events = POLLIN;
	pfd.push_back(listen_pfd);

	int yes = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

	if(bind(sockfd, (struct sockaddr*)&myaddr, sizeof myaddr) == -1)
	{
		// 0x1337BEEF: Socket bind error

		// Close server socket
		sock_close(sockfd);

		GFX->AppExit();
		sysThreadExit(0x1337BEEF);
	}

	listen(sockfd, LISTEN_BACKLOG);

	// Register FTP command handlers
	register_ftp_cmds(&m_cmd);

	// Main thread loop
	while(GFX->GetAppStatus() != APP_EXIT)
	{
		// Poll sockets (100ms timeout to allow app status checking)
		nfds_t nfds = (nfds_t)pfd.size();
		int p = poll(&pfd[0], nfds, 100);

		if(p == -1)
		{
			// 0x13371010: Socket poll error
			
			// Close all connections
			closeAll(pfd);

			// Free memory
			m_cmd.clear();
			m_clnt.clear();
			m_dataclnt.clear();
			m_datahndl.clear();
			
			GFX->AppExit();
			sysThreadExit(0x13371010);
		}

		// Loop through sockets if poll returned more than zero
		for(unsigned int i = 0; (i < nfds && p > 0); i++)
		{
			// Remove from poll if fd < 0
			if(pfd[i].fd < 0)
			{
				pfd.erase(pfd.begin() + i);
				nfds--;
				continue;
			}

			// Check if something happened on this socket
			if(pfd[i].revents == 0)
			{
				// nothing happened on this socket, move to next
				continue;
			}

			p--;

			const int fd = pfd[i].fd;

			if(fd == sockfd)
			{
				// listener socket event - new connection?
				if(pfd[i].revents & POLLIN)
				{
					// accept new connection
					int nfd = accept(sockfd, NULL, NULL);

					// add to poll
					pollfd new_pfd;
					new_pfd.fd = nfd;
					new_pfd.events = (POLLIN|POLLRDNORM);
					pfd.push_back(new_pfd);

					// add to clients
					ftp_client client;
					client.fd = nfd;
					m_clnt[fd] = client;

					// welcome
					client.response(220, APP_NAME " version " APP_VERSION, true);
					client.response(220, "by " APP_AUTHOR);
					continue;
				}
				else
				{
					// 0x1337ABCD: some error happened to the server

					// Close all connections
					closeAll(pfd);

					// Free memory
					m_cmd.clear();
					m_clnt.clear();
					m_dataclnt.clear();
					m_datahndl.clear();

					GFX->AppExit();
					sysThreadExit(0x1337ABCD);
				}
			}

			// Disconnect event
			if(pfd[i].revents & (POLLNVAL|POLLHUP|POLLERR))
			{
				// Socket disconnected - remove any references
				map<int,ftp_client>::iterator cit = m_clnt.find(fd);

				// Control connection
				if(cit != m_clnt.end())
				{
					event_client_drop(&cit->second);
					m_clnt.erase(fd);
				}
				else
				{
					// Data connection
					m_dataclnt.erase(fd);
					m_datahndl.erase(fd);
				}

				// Close socket
				sock_close(fd);

				// Remove from poll
				pfd.erase(pfd.begin() + i);
				nfds--;
				continue;
			}

			// Sending data event
			if(pfd[i].revents & (POLLOUT|POLLWRNORM))
			{
				// call data handler
				(*m_datahndl[fd])(m_dataclnt[fd], fd);
				continue;
			}

			// Receiving data event
			if(pfd[i].revents & (POLLIN|POLLRDNORM))
			{
				// check if it is a data connection
				map<int,ftp_client*>::iterator cit = m_dataclnt.find(fd);

				if(cit != m_dataclnt.end())
				{
					// call data handler
					(*m_datahndl[fd])(m_dataclnt[fd], fd);
				}
				else
				{
					// Command socket
					ftp_client* client = &m_clnt[fd];

					// Parse FTP command from command socket
					char *data;
					data = new char[CMDBUFFER];

					size_t bytes = recv(fd, data, CMDBUFFER - 1, 0);

					if(bytes <= 0)
					{
						// invalid message
						sock_close(fd);
					}
					else
					{
						// check valid command string
						if(bytes <= 2)
						{
							delete [] data;
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
						
						ftpcmd_handler::iterator it = m_cmd.find(cmd);

						if(it == m_cmd.end())
						{
							// command not found
							client->response(502, "Command not implemented");
						}
						else
						{
							// call handler
							(it->second)(client, cmd, args);

							// set last cmd (for command handler usage)
							client->last_cmd = cmd;
						}
					}

					delete [] data;
				}

				continue;
			}
		}
	}

	// Close all socket connections
	closeAll(pfd);

	// Free memory
	m_cmd.clear();
	m_clnt.clear();
	m_dataclnt.clear();
	m_datahndl.clear();

	sysThreadExit(0);
}

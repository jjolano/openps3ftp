/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jjolano@me.com> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return John Olano
 * ----------------------------------------------------------------------------
 */

#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

#include <NoRSX.h>
#include <ppu-types.h>
#include <sys/socket.h>
#include <sys/thread.h>
#include <netinet/in.h>
#include <net/net.h>
#include <net/poll.h>
#include <netdb.h>

#include "ftp.h"
#include "opf.h"
#include "ftpcmd.h"

#define CMDBUFFER			1024
#define LISTEN_BACKLOG		10
#define FD(socket)			((socket)&~SOCKET_FD_MASK)

using namespace std;

vector<pollfd> pfd;
map<int,datahandler> m_datahndl;

void sock_close(int socket)
{
	if(socket > -1)
	{
		netClose(FD(socket));
	}
}

void closeAll(vector<pollfd> &pfd)
{
	for(vector<pollfd>::iterator it = pfd.begin(); it != pfd.end(); ++it)
	{
		sock_close(it->fd);
	}
}

void ftp_client::response(unsigned int code, string message)
{
	ostringstream out;
	out << code << ' ' << message;

	custresponse(out.str());
}

void ftp_client::multiresponse(unsigned int code, string message)
{
	ostringstream out;
	out << code << '-' << message;

	custresponse(out.str());
}

void ftp_client::custresponse(string message)
{
	ostringstream out;
	out << message << '\r' << '\n';

	string str = out.str();
	const char* p = str.c_str();
	send(fd, p, out.tellp(), 0);
}

void register_data_handler(int data_fd, datahandler data_handler, int event)
{
	// add to pollfds
	pollfd data_pfd;
	data_pfd.fd = data_fd;

	if(event == FTP_DATA_EVENT_SEND)
	{
		data_pfd.events = POLLWRNORM;
	}

	if(event == FTP_DATA_EVENT_RECV)
	{
		data_pfd.events = POLLRDNORM;
	}

	pfd.push_back(data_pfd);

	// add handler
	m_datahndl[data_fd] = data_handler;
}

void ftp_main(void *arg)
{
	// Obtain graphics pointer
	NoRSX* GFX = static_cast<NoRSX*>(arg);

	// Maps and vectors
	ftpcmd_handler m_cmd;
	map<int,ftp_client> m_clnt;
	map<int,ftp_client*> m_dataclnt;

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
		// This can probably set to an indefinite timeout, but that needs
		// something to do with interrupts. Don't know about that yet.
		nfds_t nfds = (nfds_t)pfd.size();
		int p = poll(&pfd[0], nfds, 100);

		if(p == -1)
		{
			// 0x13371010: Socket poll error
			closeAll(pfd);
			GFX->AppExit();
			sysThreadExit(0x13371010);
		}

		// Loop through sockets if poll returned more than zero
		for(unsigned int i = 0; (i < nfds && p > 0); i++)
		{
			if(pfd[i].revents == 0)
			{
				// nothing happened on this socket
				continue;
			}

			// something happened on this socket
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
					new_pfd.events = POLLRDNORM;
					pfd.push_back(new_pfd);

					// add to clients
					ftp_client client;
					client.fd = nfd;
					m_clnt[fd] = client;

					// welcome
					ostringstream out;
					out << "OpenPS3FTP version " << OFTP_VERSION;
					client.multiresponse(220, out.str());

					out.str("");
					out.clear();
					out << "by John Olano (twitter: @jjolano)";
					client.response(220, out.str());
					continue;
				}
			}

			if(pfd[i].revents & (POLLNVAL|POLLHUP|POLLERR))
			{
				// Remove useless socket
				event_client_drop(&m_clnt[fd]);
				sock_close(fd);
				m_clnt.erase(fd);
				m_dataclnt.erase(fd);
				m_datahndl.erase(fd);
				pfd.erase(pfd.begin() + i);
				nfds--;
				continue;
			}

			if(pfd[i].revents & POLLWRNORM)
			{
				// data socket event
				// try to call data handler
				map<int,ftp_client*>::iterator cit = m_dataclnt.find(fd);
				map<int,datahandler>::iterator dit = m_datahndl.find(fd);

				if(cit != m_dataclnt.end() && dit != m_datahndl.end())
				{
					// call data handler
					(dit->second)(cit->second, fd);
				}
				else
				{
					// disconnect orphaned socket
					sock_close(fd);
					m_dataclnt.erase(fd);
					m_datahndl.erase(fd);
					pfd.erase(pfd.begin() + i);
					nfds--;
				}
			}

			if(pfd[i].revents & POLLRDNORM)
			{
				// client socket event
				// attempt to call data handler
				map<int,ftp_client*>::iterator cit = m_dataclnt.find(fd);

				if(cit != m_dataclnt.end())
				{
					// Data socket - get data handler
					map<int,datahandler>::iterator dit = m_datahndl.find(fd);

					if(dit != m_datahndl.end())
					{
						// call data handler
						(dit->second)(cit->second, fd);
					}
					else
					{
						// disconnect unhandled data socket
						sock_close(fd);
						m_dataclnt.erase(fd);
						m_datahndl.erase(fd);
						pfd.erase(pfd.begin() + i);
						nfds--;
					}
				}
				else
				{
					// Command socket
					ftp_client* client = &m_clnt[fd];

					// Parse FTP command from command socket
					char *data;
					data = new char[CMDBUFFER];

					int bytes = recv(fd, data, CMDBUFFER - 1, 0);

					if(bytes <= 0)
					{
						delete [] data;

						// connection dropped
						event_client_drop(&m_clnt[fd]);
						sock_close(fd);
						m_clnt.erase(fd);
						pfd.erase(pfd.begin() + i);
						nfds--;
					}
					else
					{
						// find command handler or return error
						string ftpstr(data);

						delete [] data;

						// check \r\n
						string::reverse_iterator rit = ftpstr.rbegin();

						if(*rit != '\n' || (*rit+1) != '\r')
						{
							client->response(500, "Invalid command");
							continue;
						}
						else
						{
							ftpstr.resize(ftpstr.size() - 2);
						}

						if(ftpstr.empty())
						{
							continue;
						}

						string::size_type pos = ftpstr.find(' ', 0);

						string cmd = ftpstr.substr(0, pos);
						string args;

						if(pos != string::npos)
						{
							args = ftpstr.substr(pos + 1);
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
				}
			}
		}
	}

	// Close all socket connections
	closeAll(pfd);

	sysThreadExit(0);
}

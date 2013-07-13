#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <algorithm>

#include <NoRSX.h>
#include <ppu-types.h>
#include <lv2/sysfs.h>
#include <sys/socket.h>
#include <sys/thread.h>
#include <netinet/in.h>
#include <net/net.h>
#include <net/poll.h>
#include <netdb.h>

#include "ftp.h"
#include "opf.h"
#include "ftpcmd.h"

#define CMDBUFFER			8192
#define OFTP_LISTEN_BACKLOG	10
#define FD(socket)			((socket)&~SOCKET_FD_MASK)
#define CLNTEV				(POLLIN|POLLRDNORM)

using namespace std;

void sock_close(int socket)
{
	netClose(FD(socket));
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
	
}

void ftp_main(void *arg)
{
	// Obtain graphics pointer
	NoRSX* GFX = static_cast<NoRSX*>(arg);

	// Maps and vectors
	vector<pollfd> pfd;
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
		sock_close(sockfd);
		GFX->AppExit();
		sysThreadExit(0x1337BEEF);
	}

	listen(sockfd, OFTP_LISTEN_BACKLOG);

	// Register FTP command handlers
	//register_ftp_cmds(&m);

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
					new_pfd.events = CLNTEV;
					pfd.push_back(new_pfd);

					// add to clients
					ftp_client client;
					
					m_clnt[fd] = client;
					continue;
				}
			}

			if(pfd[i].revents & (POLLNVAL|POLLHUP|POLLERR))
			{
				// Remove useless socket
				sock_close(fd);
				m_clnt.erase(fd);
				pfd.erase(pfd.begin() + i);
				nfds--;
				continue;
			}

			// Probably a client event.
			ftp_client* client = &m_clnt[fd];

			if(pfd[i].revents & CLNTEV)
			{
				// client socket event
				bool isDataSocket = false;

				// check if socket is a data socket

				if(isDataSocket)
				{
					// incoming data -> file
					
				}
				else
				{
					// Parse FTP command from command socket
					char *data;
					data = new char[CMDBUFFER];

					int bytes = recv(fd, data, CMDBUFFER - 1, 0);

					if(bytes <= 0)
					{
						// connection dropped
						sock_close(fd);
						m_clnt.erase(fd);
						pfd.erase(pfd.begin() + i);
						nfds--;
					}
					else
					{
						// find command handler or return error
						string ftpstr(data);

						string::size_type pos = ftpstr.find(' ', 0);

						string cmd;
						string args;

						if(pos == string::npos)
						{
							cmd = ftpstr;
						}
						else
						{
							cmd = ftpstr.substr(0, pos);
							args = ftpstr.substr(pos + 1);
						}

						transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
						
						ftpcmd_handler::const_iterator it = m_cmd.find(cmd);

						if(it == m_cmd.end())
						{
							// command not found
							client->response(502, "Command not implemented");
						}
						else
						{
							// call handler
							(it->second)(client, cmd, args);
						}
					}

					delete [] data;
				}
			}
		}
	}

	// Close all socket connections
	closeAll(pfd);

	sysThreadExit(0);
}

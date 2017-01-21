#include <cstdio>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>

#include <net/net.h>
#include <net/netdb.h>
#include <netinet/in.h>
#include <sys/thread.h>

#include <NoRSX.h>

#include "const.h"
#include "server.h"
#include "client.h"
#include "command.h"

using namespace std;

void server_start(void* arg)
{
    // Get NoRSX handle to be able to listen to system events.
    NoRSX* gfx;
    gfx = (NoRSX*)arg;

    // Create server variables.
    vector<pollfd> pollfds;
    map<int, Client*> clients;
    map<int, int> clients_data;
    map<string, cmdfunc> commands;

    // Register server commands.
    register_cmds(&commands);

    // Create server socket.
    int server;
    server = socket(PF_INET, SOCK_STREAM, 0);

    sockaddr_in myaddr;
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(21);
    myaddr.sin_addr.s_addr = INADDR_ANY;

    // Bind port 21 to server socket.
    if(bind(server, (struct sockaddr*)&myaddr, sizeof myaddr) != 0)
    {
        // Could not bind port to socket.
        gfx->AppExit();
        closesocket(server);
        sysThreadExit(1);
    }

    // Start listening for connections.
    listen(server, LISTEN_BACKLOG);

    // Create and add server pollfd.
    pollfd server_pollfd;
    server_pollfd.fd = server;
    server_pollfd.events = POLLIN;

    pollfds.push_back(server_pollfd);

    // Server loop.
    while(gfx->GetAppStatus())
    {
        int p;
        p = poll(&pollfds[0], (nfds_t)pollfds.size(), 250);

        if(p == -1)
        {
            // poll error
            gfx->AppExit();
            break;
        }

        if(p == 0)
        {
            // no new events
            continue;
        }

        // new event detected!
        // iterate through connected sockets
        for(vector<pollfd>::iterator pfd_it = pollfds.begin(); pfd_it != pollfds.end(); pfd_it++)
        {
            if(!p) break;

            pollfd pfd;
            pfd = *pfd_it;

            if(pfd.revents != 0)
            {
                p--;

                // handle socket events, depending on socket type
                // server
                if(pfd.fd == server)
                {
                    // incoming connection
                    int client_new = accept(server, NULL, NULL);

                    if(client_new == -1)
                    {
                        continue;
                    }

                    // create and add pollfd
                    pollfd client_pollfd;
                    client_pollfd.fd = client_new;
                    client_pollfd.events = (POLLIN|POLLRDNORM);

                    pollfds.push_back(client_pollfd);

                    // create new internal client
                    Client client(client_new, &pollfds, &clients_data);

                    // assign socket to internal client
                    clients.insert(make_pair(client_new, &client));

                    // hello!
                    client.send_code(220, "Welcome to OpenPS3FTP!");
                    continue;
                }
                else
                {
                    // check if socket is a client
                    map<int, Client*>::iterator client_it;
                    client_it = clients.find(pfd.fd);

                    if(client_it != clients.end())
                    {
                        // get client
                        Client client = *(client_it->second);

                        // check disconnect event
                        if(pfd.revents&(POLLNVAL|POLLHUP|POLLERR))
                        {
                            client.data_end();
                            closesocket(client.socket_ctrl);
                            pollfds.erase(pfd_it);
                            delete[] client.buffer;
                            clients.erase(client_it);
                            continue;
                        }

                        // check receiving event
                        if(pfd.revents&(POLLIN|POLLRDNORM))
                        {
                            int bytes = recv(client.socket_ctrl, client.buffer, CMD_BUFFER - 1, 0);

                            // check if recv was valid
                            if(bytes <= 0)
                            {
                                // socket was dropped
                                client.data_end();
                                closesocket(client.socket_ctrl);
                                pollfds.erase(pfd_it);
                                delete[] client.buffer;
                                clients.erase(client_it);
                                continue;
                            }

                            // handle commands at a basic level
                            stringstream in(client.buffer);

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
                            transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

                            // handle client command
                            client.handle_command(&commands, cmd, params);
                            continue;
                        }

                        continue;
                    }

                    // check if socket is data
                    map<int, int>::iterator cdata_it;
                    cdata_it = clients_data.find(pfd.fd);

                    if(cdata_it != clients_data.end())
                    {
                        // get client
                        map<int, Client*>::iterator client_it;
                        client_it = clients.find(cdata_it->second);

                        if(client_it != clients.end())
                        {
                            Client client = *(client_it->second);

                            // check disconnect event
                            if(pfd.revents&(POLLNVAL|POLLHUP|POLLERR))
                            {
                                client.data_end();
                                clients_data.erase(cdata_it);
                                continue;
                            }

                            // handle data operation
                            if(pfd.revents&(POLLOUT|POLLWRNORM) || pfd.revents&(POLLIN|POLLRDNORM))
                            {
                                client.handle_data();
                                continue;
                            }
                        }

                        continue;
                    }
                }
            }
        }
    }

    // Close sockets.
    for(vector<pollfd>::iterator pfd_it = pollfds.begin(); pfd_it != pollfds.end(); pfd_it++)
    {
        map<int, Client*>::iterator client_it;
        client_it = clients.find(pfd_it->fd);

        if(client_it != clients.end())
        {
            Client client = *(client_it->second);
            client.data_end();
            delete[] client.buffer;
        }

        closesocket(pfd_it->fd);
    }

    sysThreadExit(0);
}
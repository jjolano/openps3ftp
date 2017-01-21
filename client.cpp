#include <string>
#include <sstream>
#include <vector>
#include <map>

#include <net/net.h>
#include <netinet/in.h>
#include <netdb.h>

#include "const.h"
#include "server.h"
#include "client.h"
#include "command.h"

using namespace std;

void socket_send_message(int socket, string message)
{
    message += '\r';
    message += '\n';

    send(socket, message.c_str(), message.size(), 0);
}

Client::Client(int client, vector<pollfd>* pfds, map<int, int>* cdata)
{
    socket_ctrl = client;
    socket_data = -1;
    socket_pasv = -1;

    buffer = new char[CMD_BUFFER];

    pollfds = pfds;
    clients_data = cdata;
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
        
    }
    else
    {
        // no handler found
        send_code(502, "Command not supported");
    }
}

void Client::handle_data(int socket)
{
    int ret;
    ret = data_handler(socket);

    if(ret != 0)
    {
        data_callback(ret);
        data_end();
    }
}

int Client::data_start(func f, callback c, short events)
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
                closesocket(socket_data_new);
                return socket_data;
            }

            socket_data = socket_data_new;
        }
        else
        {
            // passive mode
            socket_data = accept(socket_pasv, NULL, NULL);

            closesocket(socket_pasv);
            socket_pasv = -1;
        }
    }

    data_handler = f;
    data_callback = c;

    if(buffer_data != NULL)
    {
        buffer_data = new char[DATA_BUFFER];
    }

    return socket_data;
}

void Client::data_end()
{
    closesocket(socket_data);
    closesocket(socket_pasv);

    socket_data = -1;
    socket_pasv = -1;

    data_handler = NULL;
    data_callback = NULL;

    delete[] buffer_data;
    buffer_data = NULL;
}

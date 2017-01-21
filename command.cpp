#include <iostream>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include <net/net.h>
#include <netinet/in.h>
#include <net/netdb.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "server.h"
#include "client.h"
#include "command.h"
#include "const.h"

using namespace std;

string get_working_directory(Client* client)
{
    string d;

    for(vector<string>::iterator it = client->cvar_cwd.begin(); it != client->cvar_cwd.end(); it++)
    {
        if(!(*it).empty())
        {
            d += "/";
            d += *it;
        }
    }

    if(d.empty())
    {
        d = "/";
    }

    return d;
}

void set_working_directory(Client* client, string newpath)
{
    client->cvar_cwd.clear();

    istringstream ss(newpath);
    string d;

    while(getline(ss, d, '/'))
    {
        client->cvar_cwd.push_back(d);
    }
}

string get_absolute_path(string cwd, string nd)
{
    if(nd[0] == '/')
    {
        return nd;
    }

    if(nd.size() > 0 && nd[nd.size() - 1] == '/')
	{
		nd.resize(nd.size() - 1);
	}

	if(cwd.size() == 0)
	{
		cwd = "/";
	}
	else if(cwd[cwd.size() - 1] != '/')
	{
		cwd += '/';
	}

    return cwd + nd;
}

bool file_exists(string path)
{
    sysFSStat stat;
    return (sysLv2FsStat(path.c_str(), &stat) == 0);
}

vector<unsigned short> parse_port(string args)
{
	vector<unsigned short> vec;
	stringstream ss(args);

	unsigned short i;

	while(ss >> i)
	{
		vec.push_back(i);

		if(ss.peek() == ',')
		{
			ss.ignore();
		}
	}

	return vec;
}

void cmd_noop(Client* client, string params)
{
    client->send_code(200, "NOOP ok.");
}

void cmd_clnt(Client* client, string params)
{
    client->send_code(200, params + " sucks!");
}

void cmd_acct(Client* client, string params)
{
    client->send_code(202, "Command ignored. ACCT not implemented.");
}

void cmd_feat(Client* client, string params)
{
    vector<string> feat;

    feat.push_back("REST STREAM");
    feat.push_back("PASV");
    feat.push_back("MDTM");
    feat.push_back("SIZE");

    client->send_multicode(211, "Features:");

    for(vector<string>::iterator it = feat.begin(); it != feat.end(); it++)
    {
        client->send_string(" " + *it);
    }

    client->send_code(211, "End");
}

void cmd_syst(Client* client, string params)
{
    client->send_code(215, "UNIX Type: L8");
}

void cmd_user(Client* client, string params)
{
    if(client->cvar_auth)
    {
        // already logged in
        client->send_code(230, "Already logged in");
        return;
    }

    if(params.empty())
    {
        client->send_code(501, "No username specified");
        return;
    }

    // should accept any kind of username
    client->cvar_user = params;
    client->send_code(331, "Username " + params + " OK. Password required");
}

void cmd_pass(Client* client, string params)
{
    // actual auth done here
    if(client->cvar_auth)
    {
        // already logged in
        client->send_code(230, "Already logged in");
        return;
    }

    if(client->lastcmd != "USER")
    {
        // user must be used first
        client->send_code(503, "Login with USER first");
        return;
    }

    client->cvar_auth = true;
    client->send_code(230, "Successfully logged in as " + client->cvar_user);
}

void cmd_type(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }

    client->send_code(200, "TYPE ok.");
}

void cmd_allo(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }

    client->send_code(202, "Command ignored. ALLO not implemented.");
}

void cmd_cwd(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }

    if(params.empty())
    {
        client->send_code(250, "Working directory unchanged.");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);
    
    // check if directory exists
    sysFSStat stat;
    if(sysLv2FsStat(path.c_str(), &stat) == 0 && S_ISDIR(stat.st_mode))
    {
        set_working_directory(client, path);
        client->send_code(250, "Working directory changed.");
    }
    else
    {
        client->send_code(550, "Cannot access the specified directory.");
    }
}

void cmd_pwd(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    client->send_code(257, "\"" + get_working_directory(client) + "\" is the current directory");
}

void cmd_mkd(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "No directory name specified");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    if(sysLv2FsMkdir(path.c_str(), S_IFMT|0777) == 0)
    {
        client->send_code(257, "\"" + params + "\" was successfully created");
    }
    else
    {
        client->send_code(550, "Cannot create the specified directory.");
    }
}

void cmd_rmd(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }

    if(params.empty())
    {
        client->send_code(501, "No directory name specified");
        return;
    }
    
    string path = get_absolute_path(get_working_directory(client), params);

    if(sysLv2FsRmdir(path.c_str()) == 0)
    {
        client->send_code(250, "Directory removed.");
    }
    else
    {
        client->send_code(550, "Cannot create the specified directory.");
    }
}

void cmd_cdup(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    client->cvar_cwd.pop_back();
    client->send_code(250, "Working directory changed.");
}

void cmd_pasv(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    closesocket(client->socket_pasv);

    sockaddr_in sa;
    socklen_t len = sizeof(sa);

    getsockname(client->socket_ctrl, (sockaddr*)&sa, &len);

    sa.sin_port = 0; // auto

    client->socket_pasv = socket(PF_INET, SOCK_STREAM, 0);

    if(bind(client->socket_pasv, (sockaddr*)&sa, sizeof(sa)) == -1)
    {
        closesocket(client->socket_pasv);
        client->socket_pasv = -1;

        client->send_code(425, "Cannot open data connection");
        return;
    }

    listen(client->socket_pasv, 1);
    client->cvar_rest = 0;

    getsockname(client->socket_pasv, (sockaddr*)&sa, &len);

    char pasv_msg[64];
    sprintf(pasv_msg,
        "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
        ((htonl(sa.sin_addr.s_addr) & 0xff000000) >> 24),
        ((htonl(sa.sin_addr.s_addr) & 0x00ff0000) >> 16),
        ((htonl(sa.sin_addr.s_addr) & 0x0000ff00) >>  8),
        (htonl(sa.sin_addr.s_addr) & 0x000000ff),
        ((htons(sa.sin_port) & 0xff00) >> 8),
        (htons(sa.sin_port) & 0x00ff));
    
    client->send_code(227, pasv_msg);
}

void cmd_port(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "Invalid PORT");
        return;
    }

    vector<unsigned short> portargs;
    portargs = parse_port(params);

    if(portargs.size() != 6)
    {
        client->send_code(501, "Bad PORT syntax");
    }

    client->data_end();

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(portargs[4] << 8 | portargs[5]);
    sa.sin_addr.s_addr = htonl(
        ((unsigned char)(portargs[0]) << 24) +
        ((unsigned char)(portargs[1]) << 16) +
        ((unsigned char)(portargs[2]) << 8) +
        ((unsigned char)(portargs[3]))
    );

    client->socket_data = socket(PF_INET, SOCK_STREAM, 0);

    if(connect(client->socket_data, (sockaddr*)&sa, sizeof(sa)) == 0)
    {
        client->cvar_rest = 0;
        client->send_code(200, "PORT successful");
    }
    else
    {
        closesocket(client->socket_data);
        client->socket_data = -1;

        client->send_code(434, "Cannot connect to host");
    }
}

void cmd_abor(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    client->data_end();
    client->send_code(226, "ABOR successful");
}

int data_list(Client* client)
{
    if(client->cvar_fd == -1)
    {
        client->send_code(451, "Failed to open directory");
        return -1;
    }

    sysFSDirent dirent;
    u64 read;

    if(sysLv2FsReadDir(client->cvar_fd, &dirent, &read) == -1)
    {
        client->send_code(451, "Failed to read directory");
        return -1;
    }

    if(read == 0)
    {
        client->send_code(226, "Transfer complete");
        return 1;
    }

    if(get_working_directory(client) == "/")
    {
        if(strcmp(dirent.d_name, "app_home") == 0
        || strcmp(dirent.d_name, "host_root") == 0
        || strcmp(dirent.d_name, "dev_flash2") == 0
        || strcmp(dirent.d_name, "dev_flash3") == 0)
        {
            // skip unreadable entries
            return 0;
        }
    }

    string path = get_absolute_path(get_working_directory(client), dirent.d_name);

    sysFSStat stat;
    if(sysLv2FsStat(path.c_str(), &stat) == -1)
    {
        // skip
        return 0;
    }

    char permissions[11];
    
    if(S_ISDIR(stat.st_mode))
    {
        permissions[0] = 'd';
    }
    else if(S_ISLNK(stat.st_mode))
    {
        permissions[0] = 'l';
    }
    else
    {
        permissions[0] = '-';
    }

    permissions[1] = ((stat.st_mode & S_IRUSR) ? 'r' : '-');
	permissions[2] = ((stat.st_mode & S_IWUSR) ? 'w' : '-');
	permissions[3] = ((stat.st_mode & S_IXUSR) ? 'x' : '-');
	permissions[4] = ((stat.st_mode & S_IRGRP) ? 'r' : '-');
	permissions[5] = ((stat.st_mode & S_IWGRP) ? 'w' : '-');
	permissions[6] = ((stat.st_mode & S_IXGRP) ? 'x' : '-');
	permissions[7] = ((stat.st_mode & S_IROTH) ? 'r' : '-');
	permissions[8] = ((stat.st_mode & S_IWOTH) ? 'w' : '-');
	permissions[9] = ((stat.st_mode & S_IXOTH) ? 'x' : '-');
	permissions[10] = '\0';

    char tstr[14];
	strftime(tstr, 13, "%b %e %H:%M", localtime(&stat.st_mtime));

    size_t len = sprintf(client->buffer_data,
        "%s %3d %-8d %-8d %10lu %s %s\r\n",
	    permissions, 1, 0, 0, stat.st_size, tstr, dirent.d_name);

    if(send(client->socket_data, client->buffer_data, len, 0) == -1)
    {
        client->send_code(451, "Data transfer error");
        return -1;
    }

    return 0;
}

void cmd_list(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(client->cvar_fd != -1)
    {
        client->send_code(450, "Transfer already in progress");
        return;
    }

    s32 fd;
    if(sysLv2FsOpenDir(get_working_directory(client).c_str(), &fd) == -1)
    {
        client->send_code(550, "Cannot access directory");
        return;
    }

    if(client->data_start(data_list, POLLOUT|POLLWRNORM) != -1)
    {
        client->cvar_fd = fd;
        client->send_code(150, "Accepted data connection");
    }
    else
    {
        sysLv2FsCloseDir(fd);
        client->send_code(425, "Cannot open data connection");
    }
}

int data_nlst(Client* client)
{
    if(client->cvar_fd == -1)
    {
        client->send_code(451, "Failed to open directory");
        return -1;
    }

    sysFSDirent dirent;
    u64 read;

    if(sysLv2FsReadDir(client->cvar_fd, &dirent, &read) == -1)
    {
        client->send_code(451, "Failed to read directory");
        return -1;
    }

    if(read == 0)
    {
        client->send_code(226, "Transfer complete");
        return 1;
    }

    if(get_working_directory(client) == "/")
    {
        if(strcmp(dirent.d_name, "app_home") == 0
        || strcmp(dirent.d_name, "host_root") == 0
        || strcmp(dirent.d_name, "dev_flash2") == 0
        || strcmp(dirent.d_name, "dev_flash3") == 0)
        {
            // skip unreadable entries
            return 0;
        }
    }

    string path = get_absolute_path(get_working_directory(client), dirent.d_name);

    sysFSStat stat;
    if(sysLv2FsStat(path.c_str(), &stat) == -1)
    {
        // skip
        return 0;
    }

    string data(dirent.d_name);

    data += '\r';
    data += '\n';

    if(send(client->socket_data, data.c_str(), (size_t)data.size(), 0) == -1)
    {
        client->send_code(451, "Data transfer error");
        return -1;
    }

    return 0;
}

void cmd_nlst(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(client->cvar_fd != -1)
    {
        client->send_code(450, "Transfer already in progress");
        return;
    }

    s32 fd;
    if(sysLv2FsOpenDir(get_working_directory(client).c_str(), &fd) == -1)
    {
        client->send_code(550, "Cannot access directory");
        return;
    }

    if(client->data_start(data_nlst, POLLOUT|POLLWRNORM) != -1)
    {
        client->cvar_fd = fd;
        client->send_code(150, "Accepted data connection");
    }
    else
    {
        sysLv2FsCloseDir(fd);
        client->send_code(425, "Cannot open data connection");
    }
}

int data_stor(Client* client)
{
    if(client->cvar_fd == -1)
    {
        client->send_code(451, "Failed to open file");
        return -1;
    }

    int read = recv(client->socket_data, client->buffer_data, DATA_BUFFER - 1, 0);

    if(read == -1)
    {
        client->send_code(451, "Error in data transmission");
        return -1;
    }

    if(read == 0)
    {
        client->send_code(226, "Transfer complete");
        return 1;
    }

    u64 written;

    if(sysLv2FsWrite(client->cvar_fd, client->buffer_data, (u64)read, &written) == -1
    || written < (u64)read)
    {
        client->send_code(452, "Failed to write data to file");
        return -1;
    }

    if(written == 0)
    {
        client->send_code(226, "Transfer complete");
        return 1;
    }

    return 0;
}

void cmd_stor(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(client->cvar_fd != -1)
    {
        client->send_code(450, "Transfer already in progress");
        return;
    }

    if(params.empty())
    {
        client->send_code(501, "No filename specified");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    s32 oflags = SYS_O_WRONLY;

    if(!file_exists(path))
    {
        oflags |= SYS_O_CREAT;
    }

    if(client->cvar_rest == 0)
    {
        oflags |= SYS_O_TRUNC;
    }

    s32 fd;
    if(sysLv2FsOpen(path.c_str(), oflags, &fd, (S_IFMT|0777), NULL, 0) == -1)
    {
        client->send_code(550, "Cannot access file");
        return;
    }

    u64 pos;
    sysLv2FsLSeek64(fd, client->cvar_rest, SEEK_SET, &pos);

    if(client->data_start(data_stor, POLLIN|POLLRDNORM) != -1)
    {
        client->cvar_fd = fd;
        client->send_code(150, "Accepted data connection");
    }
    else
    {
        sysLv2FsClose(fd);
        client->send_code(425, "Cannot open data connection");
    }
}

void cmd_appe(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(client->cvar_fd != -1)
    {
        client->send_code(450, "Transfer already in progress");
        return;
    }

    if(params.empty())
    {
        client->send_code(501, "No filename specified");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    if(!file_exists(path))
    {
        client->send_code(550, "File does not exist for appending");
        return;
    }

    s32 fd;
    if(sysLv2FsOpen(path.c_str(), SYS_O_WRONLY|SYS_O_APPEND, &fd, (S_IFMT|0777), NULL, 0) == -1)
    {
        client->send_code(550, "Cannot access file");
        return;
    }

    if(client->data_start(data_stor, POLLIN|POLLRDNORM) != -1)
    {
        client->cvar_fd = fd;
        client->send_code(150, "Accepted data connection");
    }
    else
    {
        sysLv2FsClose(fd);
        client->send_code(425, "Cannot open data connection");
    }
}

int data_retr(Client* client)
{
    if(client->cvar_fd == -1)
    {
        client->send_code(451, "Failed to open file");
        return -1;
    }

    u64 read;
    if(sysLv2FsRead(client->cvar_fd, client->buffer_data, DATA_BUFFER - 1, &read) == -1)
    {
        client->send_code(452, "Failed to read file");
        return -1;
    }

    if(read == 0)
    {
        client->send_code(226, "Transfer complete");
        return 1;
    }

    int written = send(client->socket_data, client->buffer_data, (size_t)read, 0);

    if(written == -1)
    {
        client->send_code(451, "Error in data transmission");
        return -1;
    }

    if(written == 0)
    {
        client->send_code(226, "Transfer complete");
        return 1;
    }

    return 0;
}

void cmd_retr(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(client->cvar_fd != -1)
    {
        client->send_code(450, "Transfer already in progress");
        return;
    }

    if(params.empty())
    {
        client->send_code(501, "No filename specified");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    if(!file_exists(path))
    {
        client->send_code(550, "File does not exist");
        return;
    }

    s32 fd;
    if(sysLv2FsOpen(path.c_str(), SYS_O_RDONLY, &fd, 0, NULL, 0) == -1)
    {
        client->send_code(550, "Cannot open file");
        return;
    }

    u64 pos;
    sysLv2FsLSeek64(fd, client->cvar_rest, SEEK_SET, &pos);

    if(client->data_start(data_retr, POLLOUT|POLLWRNORM) != -1)
    {
        client->cvar_fd = fd;
        client->send_code(150, "Accepted data connection");
    }
    else
    {
        sysLv2FsClose(fd);
        client->send_code(425, "Cannot open data connection");
    }
}

void cmd_stru(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "Invalid STRU");
        return;
    }

    if(params == "F")
    {
        client->send_code(200, "STRU command successful");
    }
    else
    {
        client->send_code(504, "STRU type not implemented");
    }
}

void cmd_mode(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "Invalid MODE");
        return;
    }

    if(params == "S")
    {
        client->send_code(200, "MODE command successful");
    }
    else
    {
        client->send_code(504, "MODE type not implemented");
    }
}

void cmd_rest(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        ostringstream out;
        out << client->cvar_rest;

        client->send_code(350, "Current restart point: " + out.str());
        return;
    }

    s64 rest = atoll(params.c_str());

    if(rest >= 0)
    {
        client->cvar_rest = rest;

        ostringstream out;
        out << client->cvar_rest;

        client->send_code(350, "Restarting at " + out.str());
    }
    else
    {
        client->cvar_rest = 0;
        client->send_code(554, "Invalid restart point");
    }
}

void cmd_dele(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "No filename specified");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    if(sysLv2FsUnlink(path.c_str()) == 0)
    {
        client->send_code(250, "File successfully removed");
    }
    else
    {
        client->send_code(550, "Cannot remove file");
    }
}

void cmd_rnfr(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "No filename specified");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    if(file_exists(path))
    {
        client->cvar_rnfr = path;
        client->send_code(350, "RNFR ready.");
    }
    else
    {
        client->send_code(550, "File does not exist");
    }
}

void cmd_rnto(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "No filename specified");
        return;
    }

    if(client->lastcmd != "RNFR")
    {
        client->send_code(503, "Use RNFR first");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    if(sysLv2FsRename(client->cvar_rnfr.c_str(), path.c_str()) == 0)
    {
        client->send_code(250, "File successfully renamed");
    }
    else
    {
        client->send_code(550, "Cannot rename file");
    }
}

void cmd_site(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "Invalid SITE");
        return;
    }

    stringstream in(params);

    string sitecmd;
    in >> sitecmd;

    transform(sitecmd.begin(), sitecmd.end(), sitecmd.begin(), ::toupper);

    if(sitecmd == "CHMOD")
    {
        string chmod;
        in >> chmod;

        if(chmod.empty())
        {
            client->send_code(501, "Invalid CHMOD");
            return;
        }

        string param, filename;
        while(in >> param)
        {
            if(!filename.empty())
            {
                filename += ' ';
            }

            filename += param;
        }

        if(filename.empty())
        {
            client->send_code(501, "No filename specified");
            return;
        }

        string path = get_absolute_path(get_working_directory(client), filename);

        if(sysLv2FsChmod(path.c_str(), S_IFMT|atoi(chmod.c_str())) == 0)
        {
            client->send_code(200, "CHMOD successful.");
        }
        else
        {
            client->send_code(550, "Cannot set file permissions");
        }
    }
}

void cmd_size(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "No filename specified");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    sysFSStat stat;
    if(sysLv2FsStat(path.c_str(), &stat) == 0)
    {
        ostringstream out;
		out << stat.st_size;

        client->send_code(213, out.str());
    }
    else
    {
        client->send_code(550, "Cannot access file");
    }
}

void cmd_mdtm(Client* client, string params)
{
    if(!client->cvar_auth)
    {
        client->send_code(530, "Not logged in");
        return;
    }
    
    if(params.empty())
    {
        client->send_code(501, "No filename specified");
        return;
    }

    string path = get_absolute_path(get_working_directory(client), params);

    sysFSStat stat;
    if(sysLv2FsStat(path.c_str(), &stat) == 0)
    {
        char tstr[15];
		strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));

        client->send_code(213, tstr);
    }
    else
    {
        client->send_code(550, "Cannot access file");
    }
}

void register_cmds(map<string, cmdfunc>* cmd_handlers)
{
    // No authorization required commands
	register_cmd(cmd_handlers, "NOOP", cmd_noop);
	register_cmd(cmd_handlers, "CLNT", cmd_clnt);
	register_cmd(cmd_handlers, "ACCT", cmd_acct);
	register_cmd(cmd_handlers, "FEAT", cmd_feat);
	register_cmd(cmd_handlers, "SYST", cmd_syst);
	register_cmd(cmd_handlers, "USER", cmd_user);
	register_cmd(cmd_handlers, "PASS", cmd_pass);

    // Authorization required commands
	register_cmd(cmd_handlers, "TYPE", cmd_type);
	register_cmd(cmd_handlers, "ALLO", cmd_allo);
	register_cmd(cmd_handlers, "CWD", cmd_cwd);
	register_cmd(cmd_handlers, "PWD", cmd_pwd);
	register_cmd(cmd_handlers, "MKD", cmd_mkd);
	register_cmd(cmd_handlers, "RMD", cmd_rmd);
	register_cmd(cmd_handlers, "CDUP", cmd_cdup);
	register_cmd(cmd_handlers, "PASV", cmd_pasv);
	register_cmd(cmd_handlers, "PORT", cmd_port);
	register_cmd(cmd_handlers, "ABOR", cmd_abor);
	register_cmd(cmd_handlers, "LIST", cmd_list);
	register_cmd(cmd_handlers, "NLST", cmd_nlst);
	register_cmd(cmd_handlers, "STOR", cmd_stor);
	register_cmd(cmd_handlers, "APPE", cmd_stor);
	register_cmd(cmd_handlers, "RETR", cmd_retr);
	register_cmd(cmd_handlers, "STRU", cmd_stru);
	register_cmd(cmd_handlers, "MODE", cmd_mode);
	register_cmd(cmd_handlers, "REST", cmd_rest);
	register_cmd(cmd_handlers, "DELE", cmd_dele);
	register_cmd(cmd_handlers, "RNFR", cmd_rnfr);
	register_cmd(cmd_handlers, "RNTO", cmd_rnto);
	register_cmd(cmd_handlers, "SITE", cmd_site);
	register_cmd(cmd_handlers, "SIZE", cmd_size);
	register_cmd(cmd_handlers, "MDTM", cmd_mdtm);
}

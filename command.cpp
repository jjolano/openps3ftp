#include <iostream>
#include <sstream>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <string>
#include <vector>
#include <map>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/memory.h>

#ifndef _PS3SDK_
#include <sys/file.h>
#include <net/poll.h>
#else
#include <sys/poll.h>
#include <cell/cell_fs.h>
#endif

#include "const.h"
#include "server.h"
#include "client.h"
#include "command.h"
#include "common.h"
#include "util.h"

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
	if(cwd.empty())
	{
		cwd = "/";
	}

	if(nd.empty())
	{
		return cwd;
	}

	if(nd[0] == '/')
	{
		return nd;
	}

	if(nd[nd.size() - 1] == '/')
	{
		// remove trailing slash
		nd.resize(nd.size() - 1);
	}

	if(cwd[cwd.size() - 1] != '/')
	{
		// cwd must have trailing slash
		cwd += "/";
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

void cmd_noop(Client* client, __attribute__((unused)) string params)
{
	client->send_code(200, "NOOP ok.");
}

void cmd_clnt(Client* client, string params)
{
	client->send_code(200, params + " sucks!");
}

void cmd_acct(Client* client, __attribute__((unused)) string params)
{
	client->send_code(202, "Command ignored. ACCT not implemented.");
}

void cmd_feat(Client* client, __attribute__((unused)) string params)
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

void cmd_syst(Client* client, __attribute__((unused)) string params)
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

void cmd_pass(Client* client, __attribute__((unused)) string params)
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

void cmd_type(Client* client, __attribute__((unused)) string params)
{
	if(!client->cvar_auth)
	{
		client->send_code(530, "Not logged in");
		return;
	}

	client->send_code(200, "TYPE ok.");
}

void cmd_allo(Client* client, __attribute__((unused)) string params)
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

	// check path length
	if(path.size() > MAX_PATH_LEN)
	{
		client->send_code(550, "Cannot access the specified directory. Path length exceeded.");
		return;
	}
	
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

void cmd_pwd(Client* client, __attribute__((unused)) string params)
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
	
	// check path length
	if(path.size() > MAX_PATH_LEN)
	{
		client->send_code(550, "Cannot access the specified directory. Path length exceeded.");
		return;
	}

	if(sysLv2FsMkdir(path.c_str(), (S_IFMT|0777)) == 0)
	{
		client->send_code(257, "\"" + path + "\" was successfully created");
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

	// check path length
	if(path.size() > MAX_PATH_LEN)
	{
		client->send_code(550, "Cannot access the specified directory. Path length exceeded.");
		return;
	}

	if(sysLv2FsRmdir(path.c_str()) == 0)
	{
		client->send_code(250, "Directory removed.");
	}
	else
	{
		client->send_code(550, "Cannot create the specified directory.");
	}
}

void cmd_cdup(Client* client, __attribute__((unused)) string params)
{
	if(!client->cvar_auth)
	{
		client->send_code(530, "Not logged in");
		return;
	}
	
	client->cvar_cwd.pop_back();
	client->send_code(250, "Working directory changed.");
}

void cmd_pasv(Client* client, __attribute__((unused)) string params)
{
	if(!client->cvar_auth)
	{
		client->send_code(530, "Not logged in");
		return;
	}
	
	if(client->socket_pasv != -1)
	{
		#ifdef _USE_LINGER_
		shutdown(client->socket_pasv, SHUT_RDWR);
		#endif

		close(client->socket_pasv);
	}

	sockaddr_in sa;
	socklen_t len = sizeof(sa);

	getsockname(client->socket_ctrl, (sockaddr*)&sa, &len);

	sa.sin_port = 0; // auto

	client->socket_pasv = socket(AF_INET, SOCK_STREAM, 0);

	// sockopts
	int optval = 1;
	setsockopt(client->socket_pasv, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	setsockopt(client->socket_pasv, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

	#ifdef _USE_LINGER_
	linger opt_linger;
	opt_linger.l_onoff = 1;
	opt_linger.l_linger = 0;
	setsockopt(client->socket_pasv, SOL_SOCKET, SO_LINGER, &opt_linger, sizeof(opt_linger));
	#endif

	if(bind(client->socket_pasv, (sockaddr*)&sa, sizeof(sa)) != 0)
	{
		#ifdef _USE_LINGER_
		shutdown(client->socket_pasv, SHUT_RDWR);
		#endif

		close(client->socket_pasv);
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
		return;
	}

	if(client->socket_data != -1)
	{
		client->send_code(450, "Data connection already active");
		return;
	}

	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_port = htons(portargs[4] << 8 | portargs[5]);
	sa.sin_addr.s_addr = htonl(
		((unsigned char)(portargs[0]) << 24) +
		((unsigned char)(portargs[1]) << 16) +
		((unsigned char)(portargs[2]) << 8) +
		((unsigned char)(portargs[3]))
	);

	client->socket_data = socket(AF_INET, SOCK_STREAM, 0);

	if(connect(client->socket_data, (sockaddr*)&sa, sizeof(sa)) == 0)
	{
		client->cvar_rest = 0;
		client->send_code(200, "Active data connection successful");
	}
	else
	{
		#ifdef _USE_LINGER_
		shutdown(client->socket_data, SHUT_RDWR);
		#endif

		close(client->socket_data);
		client->socket_data = -1;

		client->send_code(434, "Cannot connect to host");
	}
}

void cmd_abor(Client* client, __attribute__((unused)) string params)
{
	if(!client->cvar_auth)
	{
		client->send_code(530, "Not logged in");
		return;
	}
	
	client->data_end();
	client->send_code(226, "Data transfer aborted.");
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

	if(sysLv2FsReadDir(client->cvar_fd, &dirent, &read) != 0)
	{
		client->send_code(451, "Failed to read directory");
		sysLv2FsCloseDir(client->cvar_fd);
		return -1;
	}

	if(read == 0)
	{
		client->send_code(226, "Transfer complete");
		sysLv2FsCloseDir(client->cvar_fd);
		return 1;
	}

	if(get_working_directory(client) == "/")
	{
		if(strcmp(dirent.d_name, "app_home") == 0
		|| strcmp(dirent.d_name, "host_root") == 0
		|| strcmp(dirent.d_name, "dev_flash2") == 0
		|| strcmp(dirent.d_name, "dev_flash3") == 0)
		{
			return 0;
		}
	}
	
	string path = get_absolute_path(get_working_directory(client), dirent.d_name);

	sysFSStat stat;
	if(sysLv2FsStat(path.c_str(), &stat) != 0)
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
	else if(S_ISREG(stat.st_mode))
	{
		permissions[0] = '-';
	}
	else
	{
		permissions[0] = '?';

		// probably invalid, so skip it
		return 0;
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

	char tstr[16];
	strftime(tstr, 15, "%b %e %H:%M", localtime(&stat.st_mtime));

	ssize_t len = sprintf(client->buffer_data,
		"%s %3d %-10d %-10d %10lu %s %s\r\n",
		permissions, 1, stat.st_uid, stat.st_gid, stat.st_size, tstr, dirent.d_name);
	
	ssize_t written = send(client->socket_data, client->buffer_data, (size_t)len, 0);

	if(written == -1 || written < len)
	{
		client->send_code(451, "Data transfer error");
		sysLv2FsCloseDir(client->cvar_fd);
		return -1;
	}

	if(written == 0)
	{
		client->send_code(226, "Transfer complete");
		sysLv2FsCloseDir(client->cvar_fd);
		return 1;
	}

	return 0;
}

void cmd_list(Client* client, __attribute__((unused)) string params)
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
	if(sysLv2FsOpenDir(get_working_directory(client).c_str(), &fd) != 0)
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

	if(sysLv2FsReadDir(client->cvar_fd, &dirent, &read) != 0)
	{
		client->send_code(451, "Failed to read directory");
		sysLv2FsCloseDir(client->cvar_fd);
		return -1;
	}

	if(read == 0)
	{
		client->send_code(226, "Transfer complete");
		sysLv2FsCloseDir(client->cvar_fd);
		return 1;
	}

	if(get_working_directory(client) == "/")
	{
		if(strcmp(dirent.d_name, "app_home") == 0
		|| strcmp(dirent.d_name, "host_root") == 0
		|| strcmp(dirent.d_name, "dev_flash2") == 0
		|| strcmp(dirent.d_name, "dev_flash3") == 0)
		{
			return 0;
		}
	}

	string path = get_absolute_path(get_working_directory(client), dirent.d_name);

	sysFSStat stat;
	if(sysLv2FsStat(path.c_str(), &stat) != 0)
	{
		// skip
		return 0;
	}

	string data(dirent.d_name);

	data += '\r';
	data += '\n';

	ssize_t written = send(client->socket_data, data.c_str(), (size_t)data.size(), 0);

	if(written == -1 || (size_t)written < (size_t)data.size())
	{
		client->send_code(451, "Data transfer error");
		sysLv2FsCloseDir(client->cvar_fd);
		return -1;
	}

	if(written == 0)
	{
		client->send_code(226, "Transfer complete");
		sysLv2FsCloseDir(client->cvar_fd);
		return 1;
	}

	return 0;
}

void cmd_nlst(Client* client, __attribute__((unused)) string params)
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
	if(sysLv2FsOpenDir(get_working_directory(client).c_str(), &fd) != 0)
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

void aio_stor(sysFSAio* aio, s32 error, __attribute__((unused)) s32 xid, u64 size)
{
	if(error == CELL_FS_SUCCEEDED)
	{
		aio->offset += size;
		#ifdef _PS3SDK_
		aio->user_data = AIO_READY;
		#else
		aio->usrdata = AIO_READY;
		#endif
	}
	else
	{
		if(error == CELL_FS_EBUSY)
		{
			#ifdef _PS3SDK_
			aio->user_data = AIO_WAITING;
			#else
			aio->usrdata = AIO_WAITING;
			#endif

		}
		else
		{
			#ifdef _PS3SDK_
			aio->user_data = error;
			#else
			aio->usrdata = error;
			#endif
		}
	}
}

int data_stor(Client* client)
{
	if(client->cvar_fd == -1)
	{
		client->send_code(451, "Failed to open file");
		return -1;
	}

	if(client->cvar_aio.fd != -1)
	{
		#ifdef _PS3SDK_
		if(client->cvar_aio.user_data == AIO_ACTIVE)
		#else
		if(client->cvar_aio.usrdata == AIO_ACTIVE)
		#endif
		{
			// still writing
			return 0;
		}

		#ifdef _PS3SDK_
		if(client->cvar_aio.user_data == AIO_WAITING)
		#else
		if(client->cvar_aio.usrdata == AIO_WAITING)
		#endif
		{
			#ifdef _PS3SDK_
			client->cvar_aio.user_data = AIO_ACTIVE;
			#else
			client->cvar_aio.usrdata = AIO_ACTIVE;
			#endif

			s32 status = sysFsAioWrite(&client->cvar_aio, &client->cvar_aio_id, aio_stor);

			if(status == CELL_FS_EBUSY)
			{
				// IO busy, try again on next tick
				#ifdef _PS3SDK_
				client->cvar_aio.user_data = AIO_WAITING;
				#else
				client->cvar_aio.usrdata = AIO_WAITING;
				#endif
			}

			if(status != CELL_FS_SUCCEEDED)
			{
				client->send_code(452, "Failed to write data to file");
				sysLv2FsClose(client->cvar_fd);
				
				return -1;
			}

			return 0;
		}
	}

	stringstream from;
	stringstream to;
	
	if(client->cvar_fd_usetemp)
	{
		from << client->cvar_fd_tempdir;
		from << '/' << client->socket_ctrl;

		to << client->cvar_fd_movedir;
		to << '/' << client->cvar_fd_filename;
	}

	ssize_t read = recv(client->socket_data, client->buffer_data, DATA_BUFFER - 1, 0);

	if(read == -1)
	{
		sysLv2FsClose(client->cvar_fd);

		if(client->cvar_fd_usetemp)
		{
			sysLv2FsUnlink(from.str().c_str());
		}
		
		client->send_code(451, "Error in data transmission");
		return -1;
	}

	client->buffer_data[read] = '\0';

	if(client->cvar_aio.fd != -1)
	{
		#ifdef _PS3SDK_
		if(client->cvar_aio.user_data == AIO_READY)
		#else
		if(client->cvar_aio.usrdata == AIO_READY)
		#endif
		{
			#ifdef _PS3SDK_
			client->cvar_aio.user_data = AIO_ACTIVE;
			#else
			client->cvar_aio.usrdata = AIO_ACTIVE;
			#endif
			client->cvar_aio.size = read;

			s32 status = sysFsAioWrite(&client->cvar_aio, &client->cvar_aio_id, aio_stor);

			if(status == CELL_FS_EBUSY)
			{
				#ifdef _PS3SDK_
				client->cvar_aio.user_data = AIO_WAITING;
				#else
				client->cvar_aio.usrdata = AIO_WAITING;
				#endif
			}
			
			if(status != CELL_FS_SUCCEEDED)
			{
				sysLv2FsClose(client->cvar_fd);
				
				if(client->cvar_fd_usetemp)
				{
					sysLv2FsUnlink(from.str().c_str());
				}

				client->send_code(452, "Failed to write data to file");
				return -1;
			}

			if(read == 0)
			{
				// move file to destination
				sysLv2FsClose(client->cvar_fd);

				if(client->cvar_fd_usetemp)
				{
					if(sysLv2FsRename(from.str().c_str(), to.str().c_str()) != 0)
					{
						sysLv2FsUnlink(from.str().c_str());
						client->send_code(452, "Failed to write file");
						return -1;
					}
				}

				client->send_code(226, "Transfer complete");
				return 1;
			}
		}
	}
	else
	{
		u64 written;

		if(sysLv2FsWrite(client->cvar_fd, client->buffer_data, (u64)read, &written) != 0
		|| written < (u64)read)
		{
			sysLv2FsClose(client->cvar_fd);

			if(client->cvar_fd_usetemp)
			{
				sysLv2FsUnlink(from.str().c_str());
			}

			client->send_code(452, "Failed to write data to file");
			return -1;
		}

		if(written == 0)
		{
			// move file to destination
			sysLv2FsClose(client->cvar_fd);

			if(client->cvar_fd_usetemp)
			{
				if(sysLv2FsRename(from.str().c_str(), to.str().c_str()) != 0)
				{
					sysLv2FsUnlink(from.str().c_str());
					client->send_code(452, "Failed to write file");
					return -1;
				}
			}

			client->send_code(226, "Transfer complete");
			return 1;
		}
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

	// check path length
	if(path.size() > MAX_PATH_LEN || params.size() > MAX_FNAME_LEN)
	{
		client->send_code(550, "Path length exceeded.");
		return;
	}

	string temppath(path);
	string hddpath("/dev_hdd0");

	// enable special things only for hdd
	bool is_hdd = (path.compare(0, hddpath.size(), hddpath) == 0);

	u32 oflags = SYS_O_WRONLY;

	if(!file_exists(path))
	{
		if(is_hdd && !client->cvar_fd_tempdir.empty())
		{
			stringstream sc;
			sc << client->socket_ctrl;

			temppath = get_absolute_path(client->cvar_fd_tempdir, sc.str());
			client->cvar_fd_movedir = get_working_directory(client);
			client->cvar_fd_filename = params;

			if(file_exists(temppath))
			{
				oflags |= SYS_O_TRUNC;
			}
			else
			{
				oflags |= SYS_O_CREAT;
			}

			client->cvar_fd_usetemp = true;
		}
		else
		{
			oflags |= SYS_O_CREAT;
			client->cvar_fd_usetemp = false;
		}
	}
	else if(client->cvar_rest == 0)
	{
		oflags |= SYS_O_TRUNC;
	}

	s32 fd;
	if(sysLv2FsOpen(temppath.c_str(), oflags, &fd, (S_IFMT|0777), NULL, 0) != 0)
	{
		client->send_code(550, "Cannot access file");
		return;
	}

	if(oflags & SYS_O_CREAT)
	{
		sysLv2FsChmod(temppath.c_str(), (S_IFMT|0777));
	}

	u64 pos;
	sysLv2FsLSeek64(fd, client->cvar_rest, SEEK_SET, &pos);

	if(client->data_start(data_stor, POLLIN|POLLRDNORM|POLLRDBAND|POLLPRI) != -1)
	{
		#if defined(_USE_IOBUFFERS_) || defined(_PS3SDK_)
		sysFsSetIoBufferFromDefaultContainer(fd, DATA_BUFFER, SYS_FS_IO_BUFFER_PAGE_SIZE_64KB);
		#endif

		client->cvar_fd = fd;
		client->send_code(150, "Accepted data connection");

		if(client->cvar_use_aio)
		{
			if(is_hdd)
			{
				client->cvar_aio.fd = fd;
				client->cvar_aio.offset = 0;
				#ifdef _PS3SDK_
				client->cvar_aio.buf = (void*)client->buffer_data;
				client->cvar_aio.user_data = AIO_READY;
				#else
				client->cvar_aio.buffer_addr = (intptr_t)client->buffer_data;
				client->cvar_aio.usrdata = AIO_READY;
				#endif
			}
			else
			{
				client->cvar_aio.fd = -1;
			}
		}
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
	if(sysLv2FsRead(client->cvar_fd, client->buffer_data, DATA_BUFFER - 1, &read) != 0)
	{
		client->send_code(452, "Failed to read file");
		sysLv2FsClose(client->cvar_fd);
		return -1;
	}

	client->buffer_data[read] = '\0';

	ssize_t written = send(client->socket_data, client->buffer_data, (size_t)read, 0);

	if(written == -1 || (u64)written < read)
	{
		client->send_code(451, "Error in data transmission");
		sysLv2FsClose(client->cvar_fd);
		return -1;
	}

	if(written == 0)
	{
		client->send_code(226, "Transfer complete");
		sysLv2FsClose(client->cvar_fd);
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

	// check path length
	if(path.size() > MAX_PATH_LEN || params.size() > MAX_FNAME_LEN)
	{
		client->send_code(550, "Path length exceeded.");
		return;
	}

	if(!file_exists(path))
	{
		client->send_code(550, "File does not exist");
		return;
	}

	s32 fd;
	if(sysLv2FsOpen(path.c_str(), SYS_O_RDONLY, &fd, 0, NULL, 0) != 0)
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

	// check path length
	if(path.size() > MAX_PATH_LEN || params.size() > MAX_FNAME_LEN)
	{
		client->send_code(550, "Path length exceeded.");
		return;
	}

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

	// check path length
	if(path.size() > MAX_PATH_LEN || params.size() > MAX_FNAME_LEN)
	{
		client->send_code(550, "Path length exceeded.");
		return;
	}

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

	// check path length
	if(path.size() > MAX_PATH_LEN || params.size() > MAX_FNAME_LEN)
	{
		client->send_code(550, "Path length exceeded.");
		return;
	}

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

	sitecmd = string_to_upper(sitecmd);

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

		// check path length
		if(path.size() > MAX_PATH_LEN || params.size() > MAX_FNAME_LEN)
		{
			client->send_code(550, "Path length exceeded.");
			return;
		}

		if(sysLv2FsChmod(path.c_str(), (S_IFMT|atoi(chmod.c_str()))) == 0)
		{
			client->send_code(200, "Permissions changed.");
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

	// check path length
	if(path.size() > MAX_PATH_LEN || params.size() > MAX_FNAME_LEN)
	{
		client->send_code(550, "Path length exceeded.");
		return;
	}

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

	// check path length
	if(path.size() > MAX_PATH_LEN || params.size() > MAX_FNAME_LEN)
	{
		client->send_code(550, "Path length exceeded.");
		return;
	}

	sysFSStat stat;
	if(sysLv2FsStat(path.c_str(), &stat) == 0)
	{
		char tstr[16];
		strftime(tstr, 15, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));

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

	// Custom commands

}

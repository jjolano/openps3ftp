/*
 * cmd.cpp: command handler functions and data connection manager
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jjolano@me.com> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return John Olano
 * ----------------------------------------------------------------------------
 */

#include <ctime>
#include <cstring>
#include <iomanip>
#include <map>
#include <vector>
#include <utility>
#include <string>
#include <sstream>
#include <algorithm>

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>

#include "ftp.h"
#include "defs.h"

using namespace std;

struct ftp_cvars {
	bool authorized;	// Client logged in?
	string cmd;			// Used in some commands like USER and PASS
	string cwd;			// Client current working directory
	string rnfr;		// Used for RNFR command
	u64 rest;			// Used in resuming file transfers
	s32 fd;				// Data file descriptor
	int type;			// Type of data transfer
	char* buffer;		// Used in data transfers
};

map<ftp_client*,ftp_cvars> client_cvar;
map<int,ftp_client*> data_client;

bool isClientAuthorized(ftp_client* clnt)
{
	map<ftp_client*,ftp_cvars>::iterator it = client_cvar.find(clnt);
	return (it != client_cvar.end() ? (it->second).authorized : false);
}

string getAbsPath(string cwd, string nd)
{
	if(nd[0] == '/')
	{
		// already absolute
		return nd;
	}

	if(nd[nd.size() - 1] == '/')
	{
		nd.resize(nd.size() - 1);
	}

	if(cwd[cwd.size() - 1] != '/')
	{
		cwd += '/';
	}

	return cwd + nd;
}

bool fileExists(string path)
{
	sysFSStat stat;
	return (sysFsStat(path.c_str(), &stat) == 0);
}

bool isDirectory(string path)
{
	sysFSStat stat;
	return (sysFsStat(path.c_str(), &stat) == 0 && S_ISDIR(stat.st_mode));
}

vector<unsigned short> parsePORT(string args)
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

void data_list(int sock_data)
{
	ftp_client* clnt = data_client[sock_data];
	s32 fd = client_cvar[clnt].fd;

	sysFSDirent entry;
	u64 read;

	if(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
	{
		string filename(entry.d_name);

		if(client_cvar[clnt].cwd == "/"
		   && (filename == "app_home" || filename == "host_root"))
		{
			// skip app_home and host_root since they lock up
			return;
		}
		
		sysFSStat stat;
		s32 ret = sysFsStat(getAbsPath(client_cvar[clnt].cwd, filename).c_str(), &stat);

		if(ret == -1)
		{
			return;
		}

		ostringstream data_msg;

		// file type
		if(S_ISDIR(stat.st_mode))
		{
			data_msg << 'd';
		}
		else if(S_ISLNK(stat.st_mode))
		{
			data_msg << 'l';
		}
		else
		{
			data_msg << '-';
		}

		// permissions
		data_msg << ((stat.st_mode & S_IRUSR) ? 'r' : '-');
		data_msg << ((stat.st_mode & S_IWUSR) ? 'w' : '-');
		data_msg << ((stat.st_mode & S_IXUSR) ? 'x' : '-');
		data_msg << ((stat.st_mode & S_IRGRP) ? 'r' : '-');
		data_msg << ((stat.st_mode & S_IWGRP) ? 'w' : '-');
		data_msg << ((stat.st_mode & S_IXGRP) ? 'x' : '-');
		data_msg << ((stat.st_mode & S_IROTH) ? 'r' : '-');
		data_msg << ((stat.st_mode & S_IWOTH) ? 'w' : '-');
		data_msg << ((stat.st_mode & S_IXOTH) ? 'x' : '-');
		data_msg << ' ';

		// inodes
		data_msg << setw(3) << 1;
		data_msg << ' ';

		// userid
		data_msg << left << setw(8) << "root";
		data_msg << ' ';

		// groupid
		data_msg << left << setw(8) << "root";
		data_msg << ' ';

		// size
		data_msg << setw(7) << stat.st_size;
		data_msg << ' ';

		// modified
		char tstr[13];
		time_t rawtime;
		time(&rawtime);
		tm* now = localtime(&rawtime);
		tm* modified = localtime(&stat.st_mtime);

		if(modified->tm_year < now->tm_year || (now->tm_mon - modified->tm_mon) >= 5)
		{
			strftime(tstr, 12, "%b %e  %Y", modified);
		}
		else
		{
			strftime(tstr, 12, "%b %e %H:%M", modified);
		}

		data_msg << tstr;
		data_msg << ' ';

		// filename
		data_msg << filename;

		// ending
		data_msg << '\r';
		data_msg << '\n';

		// send to data socket
		if(clnt->data_send(data_msg.str().c_str(), data_msg.tellp()) <= 0)
		{
			clnt->control_sendCode(451, "Data transfer error");

			sysFsClosedir(fd);
			data_client.erase(sock_data);
			clnt->data_close();
			client_cvar[clnt].fd = -1;
		}
	}
	else
	{
		// finished directory listing
		clnt->control_sendCode(226, "Transfer complete");
		
		sysFsClosedir(fd);
		data_client.erase(sock_data);
		clnt->data_close();
		client_cvar[clnt].fd = -1;
	}
}

void data_mlsd(int sock_data)
{
	ftp_client* clnt = data_client[sock_data];
	s32 fd = client_cvar[clnt].fd;

	sysFSDirent entry;
	u64 read;

	if(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
	{
		string filename(entry.d_name);

		if(client_cvar[clnt].cwd == "/"
		   && (filename == "app_home" || filename == "host_root"))
		{
			// skip app_home and host_root since they lock up
			return;
		}

		sysFSStat stat;
		s32 ret = sysFsStat(getAbsPath(client_cvar[clnt].cwd, filename).c_str(), &stat);

		if(ret == -1)
		{
			return;
		}

		ostringstream data_msg;

		// type
		data_msg << "type=";

		if(filename == ".")
		{
			data_msg << "cdir";
		}
		else if(filename == "..")
		{
			data_msg << "pdir";
		}
		else if(S_ISDIR(stat.st_mode))
		{
			data_msg << "dir";
		}
		else
		{
			data_msg << "file";
		}

		data_msg << ';';

		// size
		data_msg << "siz" << (S_ISDIR(stat.st_mode) ? 'd' : 'e') << '=';
		data_msg << ';';

		// modified
		char tstr[15];
		strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));

		data_msg << "modify=";
		data_msg << tstr;
		data_msg << ';';

		// permissions
		data_msg << "UNIX.mode=0";
		data_msg << ((stat.st_mode & S_IRWXU) >> 6);
		data_msg << ((stat.st_mode & S_IRWXG) >> 3);
		data_msg << (stat.st_mode & S_IRWXO);
		data_msg << ';';

		// userid
		data_msg << "UNIX.uid=0;";

		// groupid
		data_msg << "UNIX.gid=0;";

		// filename
		data_msg << filename;

		// ending
		data_msg << '\r';
		data_msg << '\n';

		// send to data socket
		if(clnt->data_send(data_msg.str().c_str(), data_msg.tellp()) <= 0)
		{
			clnt->control_sendCode(451, "Data transfer error");

			sysFsClosedir(fd);
			data_client.erase(sock_data);
			clnt->data_close();
			client_cvar[clnt].fd = -1;
		}
	}
	else
	{
		// finished directory listing
		clnt->control_sendCode(226, "Transfer complete");
		
		sysFsClosedir(fd);
		data_client.erase(sock_data);
		clnt->data_close();
		client_cvar[clnt].fd = -1;
	}
}

void data_nlst(int sock_data)
{
	ftp_client* clnt = data_client[sock_data];
	s32 fd = client_cvar[clnt].fd;

	sysFSDirent entry;
	u64 read;

	if(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
	{
		// send to data socket
		string data_str(entry.d_name);
		
		data_str += '\r';
		data_str += '\n';
		
		if(clnt->data_send(data_str.c_str(), data_str.size()) <= 0)
		{
			clnt->control_sendCode(451, "Data transfer error");

			sysFsClosedir(fd);
			data_client.erase(sock_data);
			clnt->data_close();
			client_cvar[clnt].fd = -1;
		}
	}
	else
	{
		// finished directory listing
		clnt->control_sendCode(226, "Transfer complete");
		
		sysFsClosedir(fd);
		data_client.erase(sock_data);
		clnt->data_close();
		client_cvar[clnt].fd = -1;
	}
}

void data_stor(int sock_data)
{
	ftp_client* clnt = data_client[sock_data];
	s32 fd = client_cvar[clnt].fd;

	u64 pos;
	u64 written;
	int read;

	if(client_cvar[clnt].rest > 0)
	{
		sysFsLseek(fd, (s64)client_cvar[clnt].rest, SEEK_SET, &pos);
		client_cvar[clnt].rest = 0;
	}

	read = clnt->data_recv(client_cvar[clnt].buffer, DATA_BUFFER - 1);

	if(read == -1)
	{
		clnt->control_sendCode(451, "Data receive error");

		sysFsClose(fd);
		data_client.erase(sock_data);
		clnt->data_close();
		client_cvar[clnt].fd = -1;
		delete [] client_cvar[clnt].buffer;
	}

	if(read > 0)
	{
		// data available, write to disk
		if(sysFsWrite(fd, client_cvar[clnt].buffer, (u64)read, &written) == -1 || written < (u64)read)
		{
			// write error
			clnt->control_sendCode(452, "Disk write error - maybe disk is full");

			sysFsClose(fd);
			data_client.erase(sock_data);
			clnt->data_close();
			client_cvar[clnt].fd = -1;
			delete [] client_cvar[clnt].buffer;
		}
	}
	else
	{
		// finished file transfer
		clnt->control_sendCode(226, "Transfer complete");
		
		sysFsClose(fd);
		data_client.erase(sock_data);
		clnt->data_close();
		client_cvar[clnt].fd = -1;
		delete [] client_cvar[clnt].buffer;
	}
}

void data_retr(int sock_data)
{
	ftp_client* clnt = data_client[sock_data];
	s32 fd = client_cvar[clnt].fd;

	u64 pos;
	u64 read;

	if(client_cvar[clnt].rest > 0)
	{
		sysFsLseek(fd, (s64)client_cvar[clnt].rest, SEEK_SET, &pos);
		client_cvar[clnt].rest = 0;
	}

	if(sysFsRead(fd, client_cvar[clnt].buffer, DATA_BUFFER - 1, &read) == 0 && read > 0)
	{
		if((u64)clnt->data_send(client_cvar[clnt].buffer, (int)read) < read)
		{
			// send error
			clnt->control_sendCode(451, "Socket send error");
			
			sysFsClose(fd);
			data_client.erase(sock_data);
			clnt->data_close();
			client_cvar[clnt].fd = -1;
			delete [] client_cvar[clnt].buffer;
		}
	}
	else
	{
		// finished file transfer
		clnt->control_sendCode(226, "Transfer complete");
		
		sysFsClose(fd);
		data_client.erase(sock_data);
		clnt->data_close();
		client_cvar[clnt].fd = -1;
		delete [] client_cvar[clnt].buffer;
	}
}

void cmd_success(ftp_client* clnt, string cmd, string args)
{
	clnt->control_sendCode(200, cmd + " ok");
}

void cmd_success_auth(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		clnt->control_sendCode(200, cmd + " ok");
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_ignored(ftp_client* clnt, string cmd, string args)
{
	clnt->control_sendCode(202, cmd + " not implemented");
}

void cmd_ignored_auth(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		clnt->control_sendCode(202, cmd + " not implemented");
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_syst(ftp_client* clnt, string cmd, string args)
{
	clnt->control_sendCode(215, "UNIX Type: L8");
}

void cmd_quit(ftp_client* clnt, string cmd, string args)
{
	clnt->control_sendCode(221, "Goodbye");
	closesocket(clnt->sock_control);
}

// cmd_feat: server ftp extensions list
void cmd_feat(ftp_client* clnt, string cmd, string args)
{
	vector<string> feat;

	feat.push_back("REST STREAM");
	feat.push_back("PASV");
	feat.push_back("MDTM");
	feat.push_back("MLST type*;size*;sizd*;modify*;UNIX.mode*;UNIX.uid*;UNIX.gid*;");
	feat.push_back("MLSD");
	feat.push_back("SIZE");

	clnt->control_sendCode(211, "Features:", true);

	for(vector<string>::iterator it = feat.begin(); it != feat.end(); it++)
	{
		clnt->control_sendCustom(' ' + *it);
	}

	clnt->control_sendCode(211, "End");
}

// cmd_user: this will initialize the client's cvars
void cmd_user(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			ftp_cvars cvars = {};
			cvars.authorized = false;
			cvars.cmd = "USER";
			cvars.fd = -1;
			cvars.cwd = "/";
			
			client_cvar[clnt] = cvars;

			clnt->control_sendCode(331, "Username " + args + " OK. Password required");
		}
		else
		{
			clnt->control_sendCode(501, "No username specified");
		}
	}
	else
	{
		clnt->control_sendCode(230, "You are already logged in");
	}
}

// cmd_pass: this will verify username and password and set authorized flag
void cmd_pass(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		if(client_cvar[clnt].cmd == "USER")
		{
			if(!args.empty())
			{
				client_cvar[clnt].authorized = true;
				clnt->control_sendCode(230, "Successfully logged in");
			}
			else
			{
				clnt->control_sendCode(530, "Login authentication failed");
			}
		}
		else
		{
			clnt->control_sendCode(503, "Bad command sequence");
		}
	}
	else
	{
		clnt->control_sendCode(230, "You are already logged in");
	}
}

void cmd_cwd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		string path = getAbsPath(client_cvar[clnt].cwd, args);

		if(isDirectory(path))
		{
			client_cvar[clnt].cwd = path;
			clnt->control_sendCode(250, "Directory change successful");
		}
		else
		{
			clnt->control_sendCode(550, "Cannot access directory");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_pwd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		clnt->control_sendCode(257, "\"" + client_cvar[clnt].cwd + "\" is the current directory");
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_mkd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		string path = getAbsPath(client_cvar[clnt].cwd, args);

		if(sysFsMkdir(path.c_str(), 755) == 0)
		{
			clnt->control_sendCode(257, "\"" + args + "\" was successfully created");
		}
		else
		{
			clnt->control_sendCode(550, "Cannot create directory");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_rmd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		string path = getAbsPath(client_cvar[clnt].cwd, args);

		if(sysFsRmdir(path.c_str()) == 0)
		{
			clnt->control_sendCode(250, "Directory successfully removed");
		}
		else
		{
			clnt->control_sendCode(550, "Cannot create directory");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_cdup(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		size_t found = client_cvar[clnt].cwd.find_last_of('/');

		if(found == 0)
		{
			found = 1;
		}

		client_cvar[clnt].cwd = client_cvar[clnt].cwd.substr(0, found);

		clnt->control_sendCode(250, "Directory change successful");
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_pasv(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		clnt->data_close();

		sockaddr_in sa;
		socklen_t len = sizeof(sa);

		getsockname(clnt->sock_control, (sockaddr *)&sa, &len);

		sa.sin_port = 0; // automatically choose listen port

		clnt->sock_pasv = socket(PF_INET, SOCK_STREAM, 0);

		if(bind(clnt->sock_pasv, (sockaddr*)&sa, sizeof(sa)) == -1)
		{
			closesocket(clnt->sock_pasv);
			clnt->sock_pasv = -1;

			clnt->control_sendCode(425, "Cannot open data connection");
		}

		listen(clnt->sock_pasv, 1);

		// reset rest value
		client_cvar[clnt].rest = 0;

		getsockname(clnt->sock_pasv, (sockaddr*)&sa, &len);

		ostringstream out;
		out << "Entering Passive Mode (";
		out << ((htonl(sa.sin_addr.s_addr) & 0xff000000) >> 24) << ',';
		out << ((htonl(sa.sin_addr.s_addr) & 0x00ff0000) >> 16) << ',';
		out << ((htonl(sa.sin_addr.s_addr) & 0x0000ff00) >>  8) << ',';
		out << (htonl(sa.sin_addr.s_addr) & 0x000000ff) << ',';
		out << ((htons(sa.sin_port) & 0xff00) >> 8) << ',';
		out << (htons(sa.sin_port) & 0x00ff) << ')';

		clnt->control_sendCode(227, out.str());
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_port(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			clnt->data_close();

			vector<unsigned short> portargs;
			portargs = parsePORT(args);

			if(portargs.size() == 6)
			{
				sockaddr_in sa;
				sa.sin_family = AF_INET;
				sa.sin_port = htons(portargs[4] << 8 | portargs[5]);
				sa.sin_addr.s_addr = htonl(
					((unsigned char)(portargs[0]) << 24) +
					((unsigned char)(portargs[1]) << 16) +
					((unsigned char)(portargs[2]) << 8) +
					((unsigned char)(portargs[3])));

				clnt->sock_data = socket(PF_INET, SOCK_STREAM, 0);

				if(connect(clnt->sock_data, (sockaddr*)&sa, sizeof(sa)) == -1)
				{
					closesocket(clnt->sock_data);
					clnt->sock_data = -1;

					clnt->control_sendCode(425, "Cannot open data connection");
				}
				else
				{
					// reset rest value
					client_cvar[clnt].rest = 0;

					clnt->control_sendCode(200, "PORT command successful");
				}
			}
			else
			{
				clnt->control_sendCode(501, "Bad PORT syntax");
			}
		}
		else
		{
			clnt->control_sendCode(501, "No PORT arguments specifed");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_abor(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		clnt->control_sendCode(226, "ABOR command successful");

		if(client_cvar[clnt].fd != -1)
		{
			if(client_cvar[clnt].type == DATA_TYPE_DIR)
			{
				// close directory fd
				sysFsClosedir(client_cvar[clnt].fd);
			}
			else
			{
				// close file fd
				sysFsClose(client_cvar[clnt].fd);
				delete [] client_cvar[clnt].buffer;
			}

			data_client.erase(clnt->sock_data);
		}

		clnt->data_close();
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_list(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(client_cvar[clnt].fd == -1)
		{
			// attempt to open cwd
			if(sysFsOpendir(client_cvar[clnt].cwd.c_str(), &(client_cvar[clnt].fd)) == 0)
			{
				// open data connection
				if(clnt->data_open(data_list, FTP_DATA_EVENT_SEND))
				{
					// register data handler and set type dvar
					client_cvar[clnt].type = DATA_TYPE_DIR;
					data_client[clnt->sock_data] = clnt;

					clnt->control_sendCode(150, "Accepted data connection");
				}
				else
				{
					sysFsClosedir(client_cvar[clnt].fd);
					client_cvar[clnt].fd = -1;
					clnt->control_sendCode(425, "Cannot open data connection");
				}
			}
			else
			{
				// cannot open
				client_cvar[clnt].fd = -1;
				clnt->control_sendCode(550, "Cannot access directory");
			}
		}
		else
		{
			clnt->control_sendCode(450, "Transfer already in progress");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_mlsd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(client_cvar[clnt].fd == -1)
		{
			// attempt to open cwd
			if(sysFsOpendir(client_cvar[clnt].cwd.c_str(), &(client_cvar[clnt].fd)) == 0)
			{
				// open data connection
				if(clnt->data_open(data_mlsd, FTP_DATA_EVENT_SEND))
				{
					// register data handler and set type dvar
					client_cvar[clnt].type = DATA_TYPE_DIR;
					data_client[clnt->sock_data] = clnt;

					clnt->control_sendCode(150, "Accepted data connection");
				}
				else
				{
					sysFsClosedir(client_cvar[clnt].fd);
					client_cvar[clnt].fd = -1;
					clnt->control_sendCode(425, "Cannot open data connection");
				}
			}
			else
			{
				// cannot open
				client_cvar[clnt].fd = -1;
				clnt->control_sendCode(550, "Cannot access directory");
			}
		}
		else
		{
			clnt->control_sendCode(450, "Transfer already in progress");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_nlst(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(client_cvar[clnt].fd == -1)
		{
			// attempt to open cwd
			if(sysFsOpendir(client_cvar[clnt].cwd.c_str(), &(client_cvar[clnt].fd)) == 0)
			{
				// open data connection
				if(clnt->data_open(data_nlst, FTP_DATA_EVENT_SEND))
				{
					// register data handler and set type dvar
					client_cvar[clnt].type = DATA_TYPE_DIR;
					data_client[clnt->sock_data] = clnt;

					clnt->control_sendCode(150, "Accepted data connection");
				}
				else
				{
					sysFsClosedir(client_cvar[clnt].fd);
					client_cvar[clnt].fd = -1;
					clnt->control_sendCode(425, "Cannot open data connection");
				}
			}
			else
			{
				// cannot open
				client_cvar[clnt].fd = -1;
				clnt->control_sendCode(550, "Cannot access directory");
			}
		}
		else
		{
			clnt->control_sendCode(450, "Transfer already in progress");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_stor(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			if(client_cvar[clnt].fd == -1)
			{
				string path = getAbsPath(client_cvar[clnt].cwd, args);
				s32 oflags = (SYS_O_WRONLY | SYS_O_CREAT);

				// extra flag for append, set rest to 0
				if(cmd == "APPE")
				{
					client_cvar[clnt].rest = 0;
					oflags |= SYS_O_APPEND;
				}

				// extra flag for stor, if rest == 0
				if(client_cvar[clnt].rest == 0)
				{
					oflags |= SYS_O_TRUNC;
				}

				// attempt to open file
				if(sysFsOpen(path.c_str(), oflags, &(client_cvar[clnt].fd), NULL, 0) == 0)
				{
					// open data connection
					if(clnt->data_open(data_stor, FTP_DATA_EVENT_RECV))
					{
						// register data handler and set type dvar
						client_cvar[clnt].type = DATA_TYPE_FILE;
						client_cvar[clnt].buffer = new char[DATA_BUFFER];
						data_client[clnt->sock_data] = clnt;

						clnt->control_sendCode(150, "Accepted data connection");
					}
					else
					{
						sysFsClose(client_cvar[clnt].fd);
						client_cvar[clnt].fd = -1;
						clnt->control_sendCode(425, "Cannot open data connection");
					}
				}
				else
				{
					// cannot open
					client_cvar[clnt].fd = -1;
					clnt->control_sendCode(550, "Cannot access file");
				}
			}
			else
			{
				clnt->control_sendCode(450, "Transfer already in progress");
			}
		}
		else
		{
			clnt->control_sendCode(501, "No filename specified");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_retr(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			if(client_cvar[clnt].fd == -1)
			{
				string path = getAbsPath(client_cvar[clnt].cwd, args);
				s32 oflags = SYS_O_RDONLY;

				// attempt to open file
				if(sysFsOpen(path.c_str(), oflags, &(client_cvar[clnt].fd), NULL, 0) == 0)
				{
					// open data connection
					if(clnt->data_open(data_retr, FTP_DATA_EVENT_RECV))
					{
						// register data handler and set type dvar
						client_cvar[clnt].type = DATA_TYPE_FILE;
						client_cvar[clnt].buffer = new char[DATA_BUFFER];
						data_client[clnt->sock_data] = clnt;

						clnt->control_sendCode(150, "Accepted data connection");
					}
					else
					{
						sysFsClose(client_cvar[clnt].fd);
						client_cvar[clnt].fd = -1;
						clnt->control_sendCode(425, "Cannot open data connection");
					}
				}
				else
				{
					// cannot open
					client_cvar[clnt].fd = -1;
					clnt->control_sendCode(550, "Cannot access file");
				}
			}
			else
			{
				clnt->control_sendCode(450, "Transfer already in progress");
			}
		}
		else
		{
			clnt->control_sendCode(501, "No filename specified");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_stru(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(args == "F")
		{
			clnt->control_sendCode(200, "STRU command successful");
		}
		else
		{
			clnt->control_sendCode(504, "STRU type not implemented");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_mode(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(args == "S")
		{
			clnt->control_sendCode(200, "MODE command successful");
		}
		else
		{
			clnt->control_sendCode(504, "MODE type not implemented");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_rest(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		// C++11
		client_cvar[clnt].rest = strtoull(args.c_str(), NULL, 10);

		if(client_cvar[clnt].rest >= 0)
		{
			ostringstream out;
			out << client_cvar[clnt].rest;

			clnt->control_sendCode(350, "Restarting at " + out.str());
		}
		else
		{
			client_cvar[clnt].rest = 0;
			clnt->control_sendCode(554, "Invalid restart point");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_dele(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(client_cvar[clnt].cwd, args);

			if(sysFsUnlink(path.c_str()) == 0)
			{
				clnt->control_sendCode(250, "File successfully removed");
			}
			else
			{
				clnt->control_sendCode(550, "Cannot remove file");
			}
		}
		else
		{
			clnt->control_sendCode(501, "No filename specified");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_rnfr(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(client_cvar[clnt].cwd, args);

			if(fileExists(path))
			{
				client_cvar[clnt].rnfr = path;
				client_cvar[clnt].cmd = "RNFR";
				clnt->control_sendCode(350, "RNFR accepted - ready for destination");
			}
			else
			{
				clnt->control_sendCode(550, "RNFR failed - file does not exist");
			}
		}
		else
		{
			clnt->control_sendCode(501, "No filename specified");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_rnto(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(client_cvar[clnt].cmd == "RNFR")
		{
			if(!args.empty())
			{
				string path = getAbsPath(client_cvar[clnt].cwd, args);

				if(sysLv2FsRename(client_cvar[clnt].rnfr.c_str(), path.c_str()) == 0)
				{
					clnt->control_sendCode(250, "File successfully renamed");
				}
				else
				{
					clnt->control_sendCode(550, "Cannot rename file");
				}
			}
			else
			{
				clnt->control_sendCode(501, "No filename specified");
			}
		}
		else
		{
			clnt->control_sendCode(503, "Bad command sequence");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_site(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string ftpstr(args);

			string::size_type pos = ftpstr.find(' ', 0);
			string sitecmd = ftpstr.substr(0, pos);

			transform(sitecmd.begin(), sitecmd.end(), sitecmd.begin(), ::toupper);

			if(sitecmd == "CHMOD")
			{
				if(pos != string::npos)
				{
					ftpstr = ftpstr.substr(pos + 1);

					pos = ftpstr.find(' ', 0);
					string chmod = ftpstr.substr(0, pos);

					if(pos != string::npos)
					{
						args = ftpstr.substr(pos + 1);

						string path = getAbsPath(client_cvar[clnt].cwd, args);

						if(sysFsChmod(path.c_str(), atoi(chmod.c_str())) == 0)
						{
							clnt->control_sendCode(200, "Successfully set file permissions");
						}
						else
						{
							clnt->control_sendCode(550, "Cannot set file permissions");
						}
					}
					else
					{
						clnt->control_sendCode(501, "No filename specified");
					}
				}
				else
				{
					clnt->control_sendCode(501, "Bad CHMOD syntax");
				}
			}
			else
			{
				clnt->control_sendCode(504, "SITE command not implemented");
			}
		}
		else
		{
			clnt->control_sendCode(500, "Missing SITE command");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_size(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(client_cvar[clnt].cwd, args);

			sysFSStat stat;
			if(sysFsStat(path.c_str(), &stat) == 0)
			{
				ostringstream out;
				out << stat.st_size;

				clnt->control_sendCode(213, out.str());
			}
			else
			{
				clnt->control_sendCode(550, "Cannot access file");
			}
		}
		else
		{
			clnt->control_sendCode(501, "No filename specified");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void cmd_mdtm(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(client_cvar[clnt].cwd, args);

			sysFSStat stat;
			if(sysFsStat(path.c_str(), &stat) == 0)
			{
				char tstr[15];
				strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));

				clnt->control_sendCode(213, tstr);
			}
			else
			{
				clnt->control_sendCode(550, "Cannot access file");
			}
		}
		else
		{
			clnt->control_sendCode(501, "No filename specified");
		}
	}
	else
	{
		clnt->control_sendCode(530, "Not logged in");
	}
}

void register_cmd(string cmd, cmdhnd handler);

void register_cmds()
{
	// No authorization required commands
	register_cmd("NOOP", cmd_success);
	register_cmd("CLNT", cmd_success);
	register_cmd("ACCT", cmd_ignored);
	register_cmd("QUIT", cmd_quit);
	register_cmd("FEAT", cmd_feat);
	register_cmd("SYST", cmd_syst);
	register_cmd("USER", cmd_user);
	register_cmd("PASS", cmd_pass);

	// Authorization required commands
	register_cmd("TYPE", cmd_success_auth);
	register_cmd("ALLO", cmd_ignored_auth);
	register_cmd("CWD", cmd_cwd);
	register_cmd("PWD", cmd_pwd);
	register_cmd("MKD", cmd_mkd);
	register_cmd("RMD", cmd_rmd);
	register_cmd("CDUP", cmd_cdup);
	register_cmd("PASV", cmd_pasv);
	register_cmd("PORT", cmd_port);
	register_cmd("ABOR", cmd_abor);
	register_cmd("LIST", cmd_list);
	register_cmd("MLSD", cmd_mlsd);
	register_cmd("NLST", cmd_nlst);
	register_cmd("STOR", cmd_stor);
	register_cmd("APPE", cmd_stor);
	register_cmd("RETR", cmd_retr);
	register_cmd("STRU", cmd_stru);
	register_cmd("MODE", cmd_mode);
	register_cmd("REST", cmd_rest);
	register_cmd("DELE", cmd_dele);
	register_cmd("RNFR", cmd_rnfr);
	register_cmd("RNTO", cmd_rnto);
	register_cmd("SITE", cmd_site);
	register_cmd("SIZE", cmd_size);
	register_cmd("MDTM", cmd_mdtm);
}

void event_client_drop(ftp_client* clnt)
{
	if(client_cvar[clnt].fd != -1)
	{
		if(client_cvar[clnt].type == DATA_TYPE_DIR)
		{
			// close directory fd
			sysFsClosedir(client_cvar[clnt].fd);
		}
		else
		{
			// close file fd
			sysFsClose(client_cvar[clnt].fd);
			delete [] client_cvar[clnt].buffer;
		}

		data_client.erase(clnt->sock_data);
	}

	client_cvar.erase(clnt);
}

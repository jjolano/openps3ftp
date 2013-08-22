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

#include <net/net.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/thread.h>
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

bool isClientAuthorized(ftp_client* clnt)
{
	if(clnt == NULL)
	{
		return false;
	}

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

bool fileExists(string path)
{
	return (sysLv2FsStat(path.c_str(), NULL) == 0);
}

bool isDirectory(string path)
{
	sysFSStat stat;
	return (sysLv2FsStat(path.c_str(), &stat) == 0 && S_ISDIR(stat.st_mode));
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

void closedata(ftp_client* clnt)
{
	if(client_cvar[clnt].fd != -1)
	{
		if(client_cvar[clnt].type == DATA_TYPE_DIR)
		{
			sysLv2FsCloseDir(client_cvar[clnt].fd);
		}
		else
		{
			sysLv2FsClose(client_cvar[clnt].fd);
			delete [] client_cvar[clnt].buffer;
		}

		client_cvar[clnt].fd = -1;
	}

	clnt->data_close();
}

void data_list(ftp_client* clnt)
{
	sysFSDirent entry;
	u64 read;

	if(sysLv2FsReadDir(client_cvar[clnt].fd, &entry, &read) == -1)
	{
		// failed to read directory
		clnt->control_sendCode(451, "Failed to read directory");
		closedata(clnt);
		return;
	}

	if(read <= 0)
	{
		// transfer complete
		clnt->control_sendCode(226, "Transfer complete");
		closedata(clnt);
		return;
	}

	// folder-specific filter
	if(client_cvar[clnt].cwd == "/")
	{
		if(strcmp(entry.d_name, "app_home") == 0
		|| strcmp(entry.d_name, "host_root") == 0)
		{
			// these lock up directory listing when accessed
			return;
		}
	}

	// obtain file information
	sysFSStat stat;
	s32 ret = sysLv2FsStat(getAbsPath(client_cvar[clnt].cwd, entry.d_name).c_str(), &stat);

	if(ret == -1)
	{
		// skip file that failed to access for whatever reason
		return;
	}

	// prepare data message
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
	data_msg << left << setw(8) << stat.st_uid;
	data_msg << ' ';

	// groupid
	data_msg << left << setw(8) << stat.st_gid;
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
	data_msg << entry.d_name;

	// ending
	data_msg << '\r';
	data_msg << '\n';

	// send to data socket
	if(clnt->data_send(data_msg.str().c_str(), data_msg.tellp()) == -1)
	{
		clnt->control_sendCode(451, "Data transfer error");
		closedata(clnt);
	}
}

void data_mlsd(ftp_client* clnt)
{
	sysFSDirent entry;
	u64 read;

	if(sysLv2FsReadDir(client_cvar[clnt].fd, &entry, &read) == -1)
	{
		// failed to read directory
		clnt->control_sendCode(451, "Failed to read directory");
		closedata(clnt);
		return;
	}

	if(read <= 0)
	{
		// transfer complete
		clnt->control_sendCode(226, "Transfer complete");
		closedata(clnt);
		return;
	}

	// folder-specific filter
	if(client_cvar[clnt].cwd == "/")
	{
		if(strcmp(entry.d_name, "app_home") == 0
		|| strcmp(entry.d_name, "host_root") == 0)
		{
			// these lock up directory listing when accessed
			return;
		}
	}

	// obtain file information
	sysFSStat stat;
	s32 ret = sysLv2FsStat(getAbsPath(client_cvar[clnt].cwd, entry.d_name).c_str(), &stat);

	if(ret == -1)
	{
		// skip file that failed to access for whatever reason
		return;
	}

	// prepare data message
	ostringstream data_msg;

	// type
	data_msg << "type=";

	if(strcmp(entry.d_name, ".") == 0)
	{
		data_msg << "cdir";
	}
	else if(strcmp(entry.d_name, "..") == 0)
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
	data_msg << "UNIX.uid=" << stat.st_uid << ';';

	// groupid
	data_msg << "UNIX.gid=" << stat.st_gid << ';';

	// filename
	data_msg << entry.d_name;

	// ending
	data_msg << '\r';
	data_msg << '\n';

	// send to data socket
	if(clnt->data_send(data_msg.str().c_str(), data_msg.tellp()) == -1)
	{
		clnt->control_sendCode(451, "Data transfer error");
		closedata(clnt);
	}
}

void data_nlst(ftp_client* clnt)
{
	sysFSDirent entry;
	u64 read;

	if(sysLv2FsReadDir(client_cvar[clnt].fd, &entry, &read) == -1)
	{
		// failed to read directory
		clnt->control_sendCode(451, "Failed to read directory");
		closedata(clnt);
		return;
	}

	if(read <= 0)
	{
		// transfer complete
		clnt->control_sendCode(226, "Transfer complete");
		closedata(clnt);
		return;
	}

	// prepare data message
	string data_str(entry.d_name);

	data_str += '\r';
	data_str += '\n';

	// send to data socket
	if(clnt->data_send(data_str.c_str(), data_str.size()) == -1)
	{
		clnt->control_sendCode(451, "Data transfer error");
		closedata(clnt);
	}
}

void data_stor(ftp_client* clnt)
{
	u64 written;
	int read;

	read = clnt->data_recv(client_cvar[clnt].buffer, DATA_BUFFER);

	if(read > 0)
	{
		// data available, write to disk
		if(sysLv2FsWrite(client_cvar[clnt].fd, client_cvar[clnt].buffer, (u64)read, &written) == -1 || written < (u64)read)
		{
			// write error
			clnt->control_sendCode(452, "Failed to write data to file");
			closedata(clnt);
		}
	}
	else
	{
		if(read == -1)
		{
			clnt->control_sendCode(451, "Error in data transmission");
			closedata(clnt);
		}
		else
		{
			clnt->control_sendCode(226, "Transfer complete");
			closedata(clnt);
		}
	}
}

void data_retr(ftp_client* clnt)
{
	u64 read;

	if(sysLv2FsRead(client_cvar[clnt].fd, client_cvar[clnt].buffer, DATA_BUFFER, &read) == -1)
	{
		clnt->control_sendCode(452, "Failed to read data from file");
		closedata(clnt);
		return;
	}

	if(read <= 0)
	{
		clnt->control_sendCode(226, "Transfer complete");
		closedata(clnt);
		return;
	}

	if((u64)clnt->data_send(client_cvar[clnt].buffer, (int)read) < read)
	{
		clnt->control_sendCode(451, "Error in data transmission");
		closedata(clnt);
	}
}

void cmd_success(ftp_client* clnt, string cmd, string args)
{
	clnt->control_sendCode(200, cmd + " ok");
}

void cmd_success_auth(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	clnt->control_sendCode(200, cmd + " ok");
}

void cmd_ignored(ftp_client* clnt, string cmd, string args)
{
	clnt->control_sendCode(202, cmd + " not implemented");
}

void cmd_ignored_auth(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	clnt->control_sendCode(202, cmd + " not implemented");
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
	if(isClientAuthorized(clnt))
	{
		clnt->control_sendCode(230, "Already logged in");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No username specified");
		return;
	}

	ftp_cvars cvars = {};
	
	cvars.authorized = false;
	cvars.cmd = "USER";
	cvars.fd = -1;
	cvars.cwd = "/";

	client_cvar[clnt] = cvars;

	clnt->control_sendCode(331, "Username " + args + " OK. Password required");
}

// cmd_pass: this will verify username and password and set authorized flag
void cmd_pass(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		clnt->control_sendCode(230, "Already logged in");
		return;
	}

	if(client_cvar[clnt].cmd != "USER")
	{
		clnt->control_sendCode(503, "Bad command sequence");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No password specified");
		return;
	}

	client_cvar[clnt].cmd = "";
	client_cvar[clnt].authorized = true;
	clnt->control_sendCode(230, "Successfully logged in");
}

void cmd_cwd(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
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

void cmd_pwd(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
	clnt->control_sendCode(257, "\"" + client_cvar[clnt].cwd + "\" is the current directory");
}

void cmd_mkd(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
	string path = getAbsPath(client_cvar[clnt].cwd, args);

	if(sysLv2FsMkdir(path.c_str(), (S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) /* 0755 */) == 0)
	{
		clnt->control_sendCode(257, "\"" + args + "\" was successfully created");
	}
	else
	{
		clnt->control_sendCode(550, "Cannot create directory");
	}
}

void cmd_rmd(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
	string path = getAbsPath(client_cvar[clnt].cwd, args);

	if(sysLv2FsRmdir(path.c_str()) == 0)
	{
		clnt->control_sendCode(250, "Directory successfully removed");
	}
	else
	{
		clnt->control_sendCode(550, "Cannot create directory");
	}
}

void cmd_cdup(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
	size_t found = client_cvar[clnt].cwd.find_last_of('/');

	if(found == 0)
	{
		found = 1;
	}

	client_cvar[clnt].cwd = client_cvar[clnt].cwd.substr(0, found);

	clnt->control_sendCode(250, "Directory change successful");
}

void cmd_pasv(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
	closedata(clnt);

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
		return;
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

void cmd_port(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No PORT arguments specifed");
		return;
	}

	vector<unsigned short> portargs;
	portargs = parsePORT(args);

	if(portargs.size() != 6)
	{
		clnt->control_sendCode(501, "Bad PORT syntax");
		return;
	}

	closedata(clnt);

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

		clnt->control_sendCode(434, "Cannot connect to host");
	}
	else
	{
		// reset rest value
		client_cvar[clnt].rest = 0;

		clnt->control_sendCode(200, "PORT command successful");
	}
}

void cmd_abor(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	clnt->control_sendCode(226, "ABOR command successful");
	closedata(clnt);
}

void cmd_list(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(client_cvar[clnt].fd != -1)
	{
		clnt->control_sendCode(450, "Transfer already in progress");
		return;
	}

	s32 fd;
	if(sysLv2FsOpenDir(client_cvar[clnt].cwd.c_str(), &fd) == -1)
	{
		// cannot open
		clnt->control_sendCode(550, "Cannot access directory");
		return;
	}

	// open data connection
	if(clnt->data_open(data_list, DATA_EVENT_SEND))
	{
		client_cvar[clnt].fd = fd;
		client_cvar[clnt].type = DATA_TYPE_DIR;

		clnt->control_sendCode(150, "Accepted data connection");
	}
	else
	{
		sysLv2FsCloseDir(fd);
		clnt->control_sendCode(425, "Cannot open data connection");
	}
}

void cmd_mlsd(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(client_cvar[clnt].fd != -1)
	{
		clnt->control_sendCode(450, "Transfer already in progress");
		return;
	}

	s32 fd;
	if(sysLv2FsOpenDir(client_cvar[clnt].cwd.c_str(), &fd) == -1)
	{
		// cannot open
		clnt->control_sendCode(550, "Cannot access directory");
		return;
	}

	// open data connection
	if(clnt->data_open(data_mlsd, DATA_EVENT_SEND))
	{
		client_cvar[clnt].fd = fd;
		client_cvar[clnt].type = DATA_TYPE_DIR;

		clnt->control_sendCode(150, "Accepted data connection");
	}
	else
	{
		sysLv2FsCloseDir(fd);
		clnt->control_sendCode(425, "Cannot open data connection");
	}
}

void cmd_nlst(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(client_cvar[clnt].fd != -1)
	{
		clnt->control_sendCode(450, "Transfer already in progress");
		return;
	}

	s32 fd;
	if(sysLv2FsOpenDir(client_cvar[clnt].cwd.c_str(), &fd) == -1)
	{
		// cannot open
		clnt->control_sendCode(550, "Cannot access directory");
		return;
	}

	// open data connection
	if(clnt->data_open(data_nlst, DATA_EVENT_SEND))
	{
		client_cvar[clnt].fd = fd;
		client_cvar[clnt].type = DATA_TYPE_DIR;

		clnt->control_sendCode(150, "Accepted data connection");
	}
	else
	{
		sysLv2FsCloseDir(fd);
		clnt->control_sendCode(425, "Cannot open data connection");
	}
}

void cmd_stor(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(client_cvar[clnt].fd != -1)
	{
		clnt->control_sendCode(450, "Transfer already in progress");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No filename specified");
		return;
	}

	string path = getAbsPath(client_cvar[clnt].cwd, args);
	s32 oflags = (SYS_O_WRONLY | SYS_O_CREAT);

	if(cmd == "APPE")
	{
		client_cvar[clnt].rest = 0;
		oflags |= SYS_O_APPEND;
	}
	else
	{
		if(client_cvar[clnt].rest == 0)
		{
			oflags |= SYS_O_TRUNC;
		}
	}

	s32 fd;
	if(sysLv2FsOpen(path.c_str(), oflags, &fd, (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) /* 0644 */, NULL, 0) == -1)
	{
		clnt->control_sendCode(550, "Cannot access file");
		return;
	}

	if(client_cvar[clnt].rest > 0)
	{
		u64 pos;
		sysLv2FsLSeek64(fd, client_cvar[clnt].rest, SEEK_SET, &pos);
	}

	if(clnt->data_open(data_stor, DATA_EVENT_RECV))
	{
		client_cvar[clnt].buffer = new char[DATA_BUFFER];

		if(client_cvar[clnt].buffer == NULL)
		{
			sysLv2FsClose(fd);
			clnt->control_sendCode(451, "Out of memory");
			return;
		}

		client_cvar[clnt].fd = fd;
		client_cvar[clnt].type = DATA_TYPE_FILE;

		clnt->control_sendCode(150, "Accepted data connection");
	}
	else
	{
		sysLv2FsClose(fd);
		clnt->control_sendCode(425, "Cannot open data connection");
	}
}

void cmd_retr(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(client_cvar[clnt].fd != -1)
	{
		clnt->control_sendCode(450, "Transfer already in progress");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No filename specified");
		return;
	}

	string path = getAbsPath(client_cvar[clnt].cwd, args);
	s32 oflags = SYS_O_RDONLY;

	s32 fd;
	if(sysLv2FsOpen(path.c_str(), oflags, &fd, 0, NULL, 0) == -1)
	{
		clnt->control_sendCode(550, "Cannot access file");
		return;
	}

	sysFSStat stat;
	sysLv2FsFStat(fd, &stat);

	if(stat.st_size < client_cvar[clnt].rest)
	{
		clnt->control_sendCode(552, "Restart point exceeds file size");
		return;
	}

	if(client_cvar[clnt].rest > 0)
	{
		u64 pos;
		sysLv2FsLSeek64(fd, client_cvar[clnt].rest, SEEK_SET, &pos);
	}

	if(clnt->data_open(data_retr, DATA_EVENT_RECV))
	{
		client_cvar[clnt].buffer = new char[DATA_BUFFER];

		if(client_cvar[clnt].buffer == NULL)
		{
			sysLv2FsClose(fd);
			clnt->control_sendCode(451, "Out of memory");
			return;
		}
		
		client_cvar[clnt].fd = fd;
		client_cvar[clnt].type = DATA_TYPE_FILE;

		clnt->control_sendCode(150, "Accepted data connection");
	}
	else
	{
		sysLv2FsClose(fd);
		clnt->control_sendCode(425, "Cannot open data connection");
	}
}

void cmd_stru(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
	if(args == "F")
	{
		clnt->control_sendCode(200, "STRU command successful");
	}
	else
	{
		clnt->control_sendCode(504, "STRU type not implemented");
	}
}

void cmd_mode(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
	if(args == "S")
	{
		clnt->control_sendCode(200, "MODE command successful");
	}
	else
	{
		clnt->control_sendCode(504, "MODE type not implemented");
	}
}

void cmd_rest(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}
	
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

void cmd_dele(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No filename specified");
		return;
	}

	string path = getAbsPath(client_cvar[clnt].cwd, args);

	if(sysLv2FsUnlink(path.c_str()) == 0)
	{
		clnt->control_sendCode(250, "File successfully removed");
	}
	else
	{
		clnt->control_sendCode(550, "Cannot remove file");
	}
}

void cmd_rnfr(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No filename specified");
		return;
	}
	
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

void cmd_rnto(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No filename specified");
		return;
	}

	if(client_cvar[clnt].cmd != "RNFR")
	{
		clnt->control_sendCode(503, "Bad command sequence");
		return;
	}

	client_cvar[clnt].cmd = "";

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

void cmd_site(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "Missing SITE command");
		return;
	}

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

				if(sysLv2FsChmod(path.c_str(), atoi(chmod.c_str())) == 0)
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

void cmd_size(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No filename specified");
		return;
	}
	
	string path = getAbsPath(client_cvar[clnt].cwd, args);

	sysFSStat stat;
	if(sysLv2FsStat(path.c_str(), &stat) == 0)
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

void cmd_mdtm(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		clnt->control_sendCode(530, "Not logged in");
		return;
	}

	if(args.empty())
	{
		clnt->control_sendCode(501, "No filename specified");
		return;
	}

	string path = getAbsPath(client_cvar[clnt].cwd, args);

	sysFSStat stat;
	if(sysLv2FsStat(path.c_str(), &stat) == 0)
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
	closedata(clnt);
	client_cvar.erase(clnt);
}

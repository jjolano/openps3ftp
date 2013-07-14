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
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cstdio>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "ftp.h"
#include "opf.h"
#include "ftpcmd.h"

#define DATA_BUFFER		32768

#define DATA_TYPE_LIST	1
#define DATA_TYPE_MLSD	2
#define DATA_TYPE_NLST	3
#define DATA_TYPE_STOR	4
#define DATA_TYPE_RETR	5

#define register_cmd(cmd,func) m->insert(make_pair(cmd, &func))

using namespace std;

typedef struct {
	int data_fd;
	int pasv_fd;
	bool authorized;
	string cwd;
	string rnfr;
	u64 rest;
} ftp_cvars;

typedef struct {
	s32 fd;
	int type;
	char* buffer;
} ftp_dvars;

map<ftp_client*,ftp_cvars> m_cvars;
map<ftp_client*,ftp_dvars> m_dvars;

bool isClientAuthorized(ftp_client* clnt)
{
	map<ftp_client*,ftp_cvars>::iterator it = m_cvars.find(clnt);

	if(it != m_cvars.end())
	{
		return (it->second).authorized;
	}

	return false;
}

string getAbsPath(string cwd, string nd)
{
	if(nd[0] == '/')
	{
		// already absolute
		return nd;
	}

	if(cwd[cwd.size() - 1] != '/')
	{
		cwd += '/';
	}

	return cwd+nd;
}

bool fileExists(string path)
{
	sysFSStat stat;
	return sysFsStat(path.c_str(), &stat) == 0;
}

bool isDirectory(string path)
{
	sysFSStat stat;
	return (sysFsStat(path.c_str(), &stat) == 0 && S_ISDIR(stat.st_mode));
}

vector<short unsigned int> parsePORT(string args)
{
	vector<short unsigned int> vec;
	stringstream ss(args);

	short unsigned int i;

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

int getDataConnection(ftp_client* clnt)
{
	if(m_cvars[clnt].data_fd == -1)
	{
		// check if passive mode is active
		if(m_cvars[clnt].pasv_fd != -1)
		{
			// accept data connection
			m_cvars[clnt].data_fd = accept(m_cvars[clnt].pasv_fd, NULL, NULL);

			// no need for pasv listener now
			sock_close(m_cvars[clnt].pasv_fd);
			m_cvars[clnt].pasv_fd = -1;
		}
		else
		{
			// old-style PORT (active mode) connection to port 20
			sockaddr_in sa;
			socklen_t len = sizeof(sa);

			getpeername(clnt->fd, (sockaddr*)&sa, &len);
			sa.sin_port = htons(20);

			m_cvars[clnt].data_fd = socket(PF_INET, SOCK_STREAM, 0);

			if(connect(m_cvars[clnt].data_fd, (sockaddr*)&sa, sizeof(sa)) == -1)
			{
				sock_close(m_cvars[clnt].data_fd);
				m_cvars[clnt].data_fd = -1;
			}
		}
	}

	return m_cvars[clnt].data_fd;
}

void data_handler(ftp_client* clnt, int data_fd)
{
	s32 fd = m_dvars[clnt].fd;
	int status = 0;

	switch(m_dvars[clnt].type)
	{
		case DATA_TYPE_LIST:
		{
			sysFSDirent entry;
			u64 read;

			if(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
			{
				string filename(entry.d_name);

				if(m_cvars[clnt].cwd == "/"
				   && (filename == "app_home" || filename == "host_root"))
				{
					// skip app_home and host_root since they lock up
					break;
				}

				string path = getAbsPath(m_cvars[clnt].cwd, filename);
				sysFSStat stat;
				sysFsStat(path.c_str(), &stat);

				char tstr[14];
				strftime(tstr, 13, "%3.3b %2e %H:%M", localtime(&stat.st_mtime));

				ostringstream permstr;
				permstr << (S_ISDIR(stat.st_mode) ? 'd' : '-');
				permstr << ((stat.st_mode & S_IRUSR) ? 'r' : '-');
				permstr << ((stat.st_mode & S_IWUSR) ? 'w' : '-');
				permstr << ((stat.st_mode & S_IXUSR) ? 'x' : '-');
				permstr << ((stat.st_mode & S_IRGRP) ? 'r' : '-');
				permstr << ((stat.st_mode & S_IWGRP) ? 'w' : '-');
				permstr << ((stat.st_mode & S_IXGRP) ? 'x' : '-');
				permstr << ((stat.st_mode & S_IROTH) ? 'r' : '-');
				permstr << ((stat.st_mode & S_IWOTH) ? 'w' : '-');
				permstr << ((stat.st_mode & S_IXOTH) ? 'x' : '-');

				size_t len = sprintf(m_dvars[clnt].buffer, "%s   1 %-10s %-10s %10lu %s %s\r\n", permstr.str().c_str(), "root", "root", stat.st_size, tstr, filename.c_str());

				send(data_fd, m_dvars[clnt].buffer, len, 0);
			}
			else
			{
				status = 1;
			}
			break;
		}
		case DATA_TYPE_MLSD:
		{
			sysFSDirent entry;
			u64 read;

			if(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
			{
				string filename(entry.d_name);

				if(m_cvars[clnt].cwd == "/"
				   && (filename == "app_home" || filename == "host_root"))
				{
					// skip app_home and host_root since they lock up
					break;
				}

				string path = getAbsPath(m_cvars[clnt].cwd, filename);
				sysFSStat stat;
				sysFsStat(path.c_str(), &stat);

				char tstr[15];
				strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));

				string type("file");
				string typd("e");

				if(filename == ".")
				{
					type = "cdir";
				}
				else if(filename == "..")
				{
					type = "pdir";
				}

				if(S_ISDIR(stat.st_mode))
				{
					type = "dir";
					typd = "d";
				}

				ostringstream permstr;
				permstr << (((stat.st_mode & S_IRUSR) != 0) * 4 + ((stat.st_mode & S_IWUSR) != 0) * 2 + ((stat.st_mode & S_IXUSR) != 0) * 1);
				permstr << (((stat.st_mode & S_IRGRP) != 0) * 4 + ((stat.st_mode & S_IWGRP) != 0) * 2 + ((stat.st_mode & S_IXGRP) != 0) * 1);
				permstr << (((stat.st_mode & S_IROTH) != 0) * 4 + ((stat.st_mode & S_IWOTH) != 0) * 2 + ((stat.st_mode & S_IXOTH) != 0) * 1);

				size_t len = sprintf(m_dvars[clnt].buffer, "type=%s;siz%s=%lu;modify=%s;UNIX.mode=0%s;UNIX.uid=0;UNIX.gid=0;%s\r\n", type.c_str(), typd.c_str(), stat.st_size, tstr, permstr.str().c_str(), filename.c_str());

				send(data_fd, m_dvars[clnt].buffer, len, 0);
			}
			else
			{
				status = 1;
			}
			break;
		}
		case DATA_TYPE_NLST:
		{
			sysFSDirent entry;
			u64 read;

			if(sysFsReaddir(fd, &entry, &read) == 0 && read > 0)
			{
				size_t len = sprintf(m_dvars[clnt].buffer, "%s\r\n", entry.d_name);
				send(data_fd, m_dvars[clnt].buffer, len, 0);
			}
			else
			{
				status = 1;
			}
			break;
		}
		case DATA_TYPE_STOR:
		{
			u64 pos;
			u64 written;
			u64 read;

			if(m_cvars[clnt].rest > 0)
			{
				sysFsLseek(fd, (s64)m_cvars[clnt].rest, SEEK_SET, &pos);
				m_cvars[clnt].rest = 0;
			}

			read = (u64)recv(data_fd, m_dvars[clnt].buffer, DATA_BUFFER, MSG_WAITALL);

			if(read > 0)
			{
				// data available, write to disk
				s32 ret = sysFsWrite(fd, m_dvars[clnt].buffer, read, &written);

				if(ret != 0 || written < read)
				{
					// write error
					status = -1;
				}
			}
			else
			{
				status = 1;
			}
			break;
		}
		case DATA_TYPE_RETR:
		{
			u64 pos;
			u64 read;

			if(m_cvars[clnt].rest > 0)
			{
				sysFsLseek(fd, (s64)m_cvars[clnt].rest, SEEK_SET, &pos);
				m_cvars[clnt].rest = 0;
			}

			s32 ret = sysFsRead(fd, m_dvars[clnt].buffer, DATA_BUFFER, &read);

			if(ret == 0 && read > 0)
			{
				u64 sent = (u64)send(data_fd, m_dvars[clnt].buffer, (size_t)read, 0);

				if(sent < read)
				{
					// send error
					status = -1;
				}
			}
			else
			{
				status = 1;
			}
			break;
		}
		default:
			status = 2; // some random
	}

	if(status != 0)
	{
		sock_close(data_fd);
		delete [] m_dvars[clnt].buffer;
		m_cvars[clnt].data_fd = -1;

		switch(m_dvars[clnt].type)
		{
			case DATA_TYPE_LIST:
			case DATA_TYPE_MLSD:
			case DATA_TYPE_NLST:
				sysFsClosedir(m_dvars[clnt].fd);
				break;
			default:
				sysFsClose(m_dvars[clnt].fd);
		}

		switch(status)
		{
			case 1:
				clnt->response(226, "Transfer complete");
				break;
			case -1:
				clnt->response(451, "Data process error");
				break;
		}
	}
}

void cmd_success(ftp_client* clnt, string cmd, string args)
{
	ostringstream out;
	out << cmd << " OK";
	clnt->response(200, out.str());
}

void cmd_success_auth(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		ostringstream out;
		out << cmd << " OK";
		clnt->response(200, out.str());
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_ignored(ftp_client* clnt, string cmd, string args)
{
	ostringstream out;
	out << cmd << " ignored";
	clnt->response(202, out.str());
}

void cmd_ignored_auth(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		ostringstream out;
		out << cmd << " ignored";
		clnt->response(202, out.str());
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_syst(ftp_client* clnt, string cmd, string args)
{
	clnt->response(215, "UNIX Type: L8");
}

void cmd_quit(ftp_client* clnt, string cmd, string args)
{
	clnt->response(221, "Goodbye");
	sock_close(clnt->fd);
}

// cmd_feat: server feature list
void cmd_feat(ftp_client* clnt, string cmd, string args)
{
	vector<string> feat;

	feat.push_back("REST STREAM");
	feat.push_back("PASV");
	feat.push_back("PORT");
	feat.push_back("MDTM");
	feat.push_back("MLSD");
	feat.push_back("SIZE");
	feat.push_back("SITE CHMOD");
	feat.push_back("APPE");
	feat.push_back("MLST type*;size*;sizd*;modify*;UNIX.mode*;UNIX.uid*;UNIX.gid*;");

	clnt->multiresponse(211, "Features:");

	ostringstream out;

	for(vector<string>::iterator it = feat.begin(); it != feat.end(); it++)
	{
		out.str("");
		out.clear();
		out << ' ' << *it;

		clnt->custresponse(out.str());
	}

	clnt->response(211, "End");
}

// cmd_user: this will initialize the client's cvars
void cmd_user(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		m_cvars[clnt].data_fd = -1;
		m_cvars[clnt].pasv_fd = -1;
		m_cvars[clnt].authorized = false;
		m_cvars[clnt].rest = 0;

		ostringstream out;
		out << "Username " << args << " OK. Password required";

		clnt->response(331, out.str());
	}
	else
	{
		clnt->response(230, "You are already logged in");
	}
}

// cmd_pass: this will verify username and password and set authorized flag
void cmd_pass(ftp_client* clnt, string cmd, string args)
{
	if(!isClientAuthorized(clnt))
	{
		if(clnt->last_cmd == "USER")
		{
			// set authorized flag
			m_cvars[clnt].authorized = true;

			clnt->response(230, "Successfully logged in");
		}
		else
		{
			clnt->response(503, "Bad command sequence");
		}
	}
	else
	{
		clnt->response(230, "You are already logged in");
	}
}

void cmd_cwd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		string path = getAbsPath(m_cvars[clnt].cwd, args);

		if(isDirectory(path))
		{
			m_cvars[clnt].cwd = path;
			clnt->response(250, "Directory change successful");
		}
		else
		{
			clnt->response(550, "Cannot access directory");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_pwd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		ostringstream out;
		out << "\"" << m_cvars[clnt].cwd << "\" is the current directory";

		clnt->response(257, out.str());
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_mkd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		string path = getAbsPath(m_cvars[clnt].cwd, args);

		if(sysFsMkdir(path.c_str(), 755) == 0)
		{
			ostringstream out;
			out << "\"" << args << "\" was successfully created";

			clnt->response(257, out.str());
		}
		else
		{
			clnt->response(550, "Cannot create directory");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_rmd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		string path = getAbsPath(m_cvars[clnt].cwd, args);

		if(sysFsRmdir(path.c_str()) == 0)
		{
			clnt->response(250, "Directory successfully removed");
		}
		else
		{
			clnt->response(550, "Cannot create directory");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_cdup(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		size_t found = m_cvars[clnt].cwd.find_last_of('/');

		if(found == 0)
		{
			found = 1;
		}

		m_cvars[clnt].cwd = m_cvars[clnt].cwd.substr(0, found);

		clnt->response(250, "Directory change successful");
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_pasv(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		sock_close(m_cvars[clnt].data_fd);
		sock_close(m_cvars[clnt].pasv_fd);

		m_cvars[clnt].data_fd = -1;

		sockaddr_in sa;
		socklen_t len = sizeof(sa);

		getsockname(clnt->fd, (sockaddr *)&sa, &len);

		sa.sin_port = 0; // automatically choose listen port

		m_cvars[clnt].pasv_fd = socket(PF_INET, SOCK_STREAM, 0);

		int yes = 1;
		setsockopt(m_cvars[clnt].pasv_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		if(bind(m_cvars[clnt].pasv_fd, (sockaddr*)&sa, sizeof(sa)) == -1)
		{
			sock_close(m_cvars[clnt].pasv_fd);
			m_cvars[clnt].pasv_fd = -1;

			clnt->response(425, "Failed to enter passive mode");
		}

		listen(m_cvars[clnt].pasv_fd, 1);

		// reset rest value
		m_cvars[clnt].rest = 0;

		getsockname(m_cvars[clnt].pasv_fd, (sockaddr*)&sa, &len);

		ostringstream out;
		out << "Entering Passive Mode (";
		out << ((htonl(sa.sin_addr.s_addr) & 0xff000000) >> 24) << ',';
		out << ((htonl(sa.sin_addr.s_addr) & 0x00ff0000) >> 16) << ',';
		out << ((htonl(sa.sin_addr.s_addr) & 0x0000ff00) >>  8) << ',';
		out << (htonl(sa.sin_addr.s_addr) & 0x000000ff) << ',';
		out << ((htons(sa.sin_port) & 0xff00) >> 8) << ',';
		out << (htons(sa.sin_port) & 0x00ff) << ')';

		clnt->response(227, out.str());
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_port(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			sock_close(m_cvars[clnt].data_fd);
			sock_close(m_cvars[clnt].pasv_fd);

			m_cvars[clnt].data_fd = -1;
			m_cvars[clnt].pasv_fd = -1;

			vector<short unsigned int> portargs;
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

				m_cvars[clnt].data_fd = socket(PF_INET, SOCK_STREAM, 0);

				if(connect(m_cvars[clnt].data_fd, (sockaddr*)&sa, sizeof(sa)) == -1)
				{
					sock_close(m_cvars[clnt].data_fd);
					m_cvars[clnt].data_fd = -1;

					clnt->response(425, "Failed PORT connection");
				}
				else
				{
					// reset rest value
					m_cvars[clnt].rest = 0;

					clnt->response(200, "PORT command successful");
				}
			}
			else
			{
				clnt->response(501, "Invalid PORT connection information");
			}
		}
		else
		{
			clnt->response(501, "No PORT connection information");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_abor(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		sock_close(m_cvars[clnt].data_fd);
		sock_close(m_cvars[clnt].pasv_fd);

		m_cvars[clnt].data_fd = -1;
		m_cvars[clnt].pasv_fd = -1;

		clnt->response(225, "ABOR command successful");
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_list(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		// attempt to open cwd
		if(sysFsOpendir(m_cvars[clnt].cwd.c_str(), &m_dvars[clnt].fd) == 0)
		{
			// grab data connection
			int data = getDataConnection(clnt);

			if(data != -1)
			{
				// register data handler and set type dvar
				m_dvars[clnt].type = DATA_TYPE_LIST;
				m_dvars[clnt].buffer = new char[DATA_BUFFER];
				register_data_handler(data, data_handler, FTP_DATA_EVENT_SEND);
				clnt->response(150, "Accepted data connection");
			}
			else
			{
				sysFsClosedir(m_dvars[clnt].fd);
				m_dvars.erase(clnt);
				clnt->response(425, "Cannot open data connection");
			}
		}
		else
		{
			// cannot open
			m_dvars.erase(clnt);
			clnt->response(550, "Cannot access directory");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_mlsd(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		// attempt to open cwd
		if(sysFsOpendir(m_cvars[clnt].cwd.c_str(), &m_dvars[clnt].fd) == 0)
		{
			// grab data connection
			int data = getDataConnection(clnt);

			if(data != -1)
			{
				// register data handler and set type dvar
				m_dvars[clnt].type = DATA_TYPE_MLSD;
				m_dvars[clnt].buffer = new char[DATA_BUFFER];
				register_data_handler(data, data_handler, FTP_DATA_EVENT_SEND);
				clnt->response(150, "Accepted data connection");
			}
			else
			{
				sysFsClosedir(m_dvars[clnt].fd);
				m_dvars.erase(clnt);
				clnt->response(425, "Cannot open data connection");
			}
		}
		else
		{
			// cannot open
			m_dvars.erase(clnt);
			clnt->response(550, "Cannot access directory");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_nlst(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		// attempt to open cwd
		if(sysFsOpendir(m_cvars[clnt].cwd.c_str(), &m_dvars[clnt].fd) == 0)
		{
			// grab data connection
			int data = getDataConnection(clnt);

			if(data != -1)
			{
				// register data handler and set type dvar
				m_dvars[clnt].type = DATA_TYPE_NLST;
				m_dvars[clnt].buffer = new char[DATA_BUFFER];
				register_data_handler(data, data_handler, FTP_DATA_EVENT_SEND);
				clnt->response(150, "Accepted data connection");
			}
			else
			{
				sysFsClosedir(m_dvars[clnt].fd);
				m_dvars.erase(clnt);
				clnt->response(425, "Cannot open data connection");
			}
		}
		else
		{
			// cannot open
			m_dvars.erase(clnt);
			clnt->response(550, "Cannot access directory");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_stor(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(m_cvars[clnt].cwd, args);
			s32 oflags = (SYS_O_WRONLY | SYS_O_CREAT);

			// extra flag for append, set rest to 0
			if(cmd == "APPE")
			{
				m_cvars[clnt].rest = 0;
				oflags |= SYS_O_APPEND;
			}

			// extra flag for stor, if rest == 0
			if(m_cvars[clnt].rest == 0)
			{
				oflags |= SYS_O_TRUNC;
			}

			// attempt to open file
			if(sysFsOpen(path.c_str(), oflags, &m_dvars[clnt].fd, NULL, 0) == 0)
			{
				// grab data connection
				int data = getDataConnection(clnt);

				if(data != -1)
				{
					// register data handler and set type dvar
					m_dvars[clnt].type = DATA_TYPE_STOR;
					m_dvars[clnt].buffer = new char[DATA_BUFFER];
					register_data_handler(data, data_handler, FTP_DATA_EVENT_RECV);
					clnt->response(150, "Accepted data connection");
				}
				else
				{
					sysFsClose(m_dvars[clnt].fd);
					m_dvars.erase(clnt);
					clnt->response(425, "Cannot open data connection");
				}
			}
			else
			{
				// cannot open
				m_dvars.erase(clnt);
				clnt->response(550, "Cannot access directory");
			}
		}
		else
		{
			clnt->response(501, "No filename specified");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_retr(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(m_cvars[clnt].cwd, args);
			s32 oflags = SYS_O_RDONLY;

			// attempt to open file
			if(sysFsOpen(path.c_str(), oflags, &m_dvars[clnt].fd, NULL, 0) == 0)
			{
				// grab data connection
				int data = getDataConnection(clnt);

				if(data != -1)
				{
					// register data handler and set type dvar
					m_dvars[clnt].type = DATA_TYPE_RETR;
					m_dvars[clnt].buffer = new char[DATA_BUFFER];
					register_data_handler(data, data_handler, FTP_DATA_EVENT_SEND);
					clnt->response(150, "Accepted data connection");
				}
				else
				{
					sysFsClose(m_dvars[clnt].fd);
					m_dvars.erase(clnt);
					clnt->response(425, "Cannot open data connection");
				}
			}
			else
			{
				// cannot open
				m_dvars.erase(clnt);
				clnt->response(550, "Cannot access directory");
			}
		}
		else
		{
			clnt->response(501, "No filename specified");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_stru(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(args == "F")
		{
			clnt->response(200, "STRU command successful");
		}
		else
		{
			clnt->response(504, "STRU command failed");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_mode(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(args == "S")
		{
			clnt->response(200, "MODE command successful");
		}
		else
		{
			clnt->response(504, "MODE command failed");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_rest(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		// C++11
		m_cvars[clnt].rest = strtoull(args.c_str(), NULL, 10);

		if(m_cvars[clnt].rest >= 0)
		{
			ostringstream out;
			out << "Restarting at " << m_cvars[clnt].rest;

			clnt->response(350, out.str());
		}
		else
		{
			m_cvars[clnt].rest = 0;
			clnt->response(554, "Invalid restart point");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_dele(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(m_cvars[clnt].cwd, args);

			if(sysFsUnlink(path.c_str()) == 0)
			{
				clnt->response(250, "File successfully removed");
			}
			else
			{
				clnt->response(550, "Cannot remove file");
			}
		}
		else
		{
			clnt->response(501, "No filename specified");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_rnfr(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(m_cvars[clnt].cwd, args);

			if(fileExists(path))
			{
				m_cvars[clnt].rnfr = path;
				clnt->response(350, "RNFR accepted - ready for destination");
			}
			else
			{
				clnt->response(550, "RNFR failed - file does not exist");
			}
		}
		else
		{
			clnt->response(501, "No filename specified");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_rnto(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(clnt->last_cmd == "RNFR")
		{
			if(!args.empty())
			{
				string path = getAbsPath(m_cvars[clnt].cwd, args);

				if(sysLv2FsRename(m_cvars[clnt].rnfr.c_str(), path.c_str()) == 0)
				{
					clnt->response(250, "File successfully renamed");
				}
				else
				{
					clnt->response(550, "Cannot rename file");
				}
			}
			else
			{
				clnt->response(501, "No filename specified");
			}
		}
		else
		{
			clnt->response(503, "Bad command sequence");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
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

						string path = getAbsPath(m_cvars[clnt].cwd, args);

						if(sysFsChmod(path.c_str(), atoi(chmod.c_str())) == 0)
						{
							clnt->response(250, "Successfully set file permissions");
						}
						else
						{
							clnt->response(550, "Cannot set file permissions");
						}
					}
					else
					{
						clnt->response(501, "Bad command syntax");
					}
				}
				else
				{
					clnt->response(501, "Bad command syntax");
				}
			}
			else
			{
				clnt->response(502, "SITE command not implemented");
			}
		}
		else
		{
			clnt->response(214, "Available SITE commands: CHMOD");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_size(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(m_cvars[clnt].cwd, args);

			sysFSStat stat;
			if(sysFsStat(path.c_str(), &stat) == 0)
			{
				ostringstream out;
				out << stat.st_size;

				clnt->response(213, out.str());
			}
			else
			{
				clnt->response(550, "Cannot access file");
			}
		}
		else
		{
			clnt->response(501, "No filename specified");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void cmd_mdtm(ftp_client* clnt, string cmd, string args)
{
	if(isClientAuthorized(clnt))
	{
		if(!args.empty())
		{
			string path = getAbsPath(m_cvars[clnt].cwd, args);

			sysFSStat stat;
			if(sysFsStat(path.c_str(), &stat) == 0)
			{
				char tstr[15];
				strftime(tstr, 14, "%Y%m%d%H%M%S", localtime(&stat.st_mtime));

				string s(tstr);

				clnt->response(213, s);
			}
			else
			{
				clnt->response(550, "Cannot access file");
			}
		}
		else
		{
			clnt->response(501, "No filename specified");
		}
	}
	else
	{
		clnt->response(530, "Not logged in");
	}
}

void register_ftp_cmds(ftpcmd_handler* m)
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
	sock_close(m_cvars[clnt].data_fd);
	sock_close(m_cvars[clnt].pasv_fd);

	if(m_dvars[clnt].fd != -1)
	{
		switch(m_dvars[clnt].type)
		{
			case DATA_TYPE_LIST:
			case DATA_TYPE_MLSD:
			case DATA_TYPE_NLST:
				sysFsClosedir(m_dvars[clnt].fd);
				break;
			default:
				sysFsClose(m_dvars[clnt].fd);
		}
	}

	delete [] m_dvars[clnt].buffer;

	m_cvars.erase(clnt);
	m_dvars.erase(clnt);
}

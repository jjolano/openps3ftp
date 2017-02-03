#pragma once

#include <string>
#include <vector>
#include <map>

#include <sys/memory.h>

#ifndef _PS3SDK_
#include <net/poll.h>
#include <lv2/sysfs.h>
#else
#include <cell/cell_fs.h>
#endif

#include "types.h"
#include "common.h"

using namespace std;

class Client {
private:
	data_callback data_handler;
	vector<pollfd>* pollfds;
	map<int, Client*>* clients_data;
	sys_mem_container_t buffer_io;

public:
	int socket_ctrl;
	int socket_data;
	int socket_pasv;
	char* buffer;
	char* buffer_data;
	string lastcmd;

	bool cvar_auth;
	string cvar_user;
	vector<string> cvar_cwd;
	string cvar_rnfr;
	u64 cvar_rest;
	s32 cvar_fd;
	string cvar_fd_tempdir;
	string cvar_fd_movedir;
	string cvar_fd_filename;
	bool cvar_fd_usetemp;
	bool cvar_use_aio;
	sysFSAio cvar_aio;
	s32 cvar_aio_id;

	Client(int sock, vector<pollfd>* pfds, map<int, Client*>* cdata);
	~Client(void);
	
	void send_string(string message);
	void send_code(int code, string message);
	void send_multicode(int code, string message);
	void handle_command(map<string, cmd_callback>* cmds, string cmd, string params);
	void handle_data(void);
	int data_start(data_callback callback, short events);
	void data_end(void);
	void set_io_buffer(s32 fd);
};

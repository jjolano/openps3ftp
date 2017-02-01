#pragma once

#include <string>
#include <vector>
#include <map>

#include "common.h"

#ifndef _PS3SDK_
#include <net/poll.h>
#include <lv2/sysfs.h>
#else
#include <cell/cell_fs.h>
#endif

#include "command.h"

using namespace std;

typedef int (*func)(Client*);

class Client {
private:
	func data_handler;
	vector<pollfd>* pollfds;
	map<int, Client*>* clients;
	map<int, Client*>* clients_data;

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
	bool cvar_use_aio;
	sysFSAio cvar_aio;
	s32 cvar_aio_id;

	Client(int, vector<pollfd>*, map<int, Client*>*, map<int, Client*>*);
	~Client(void);
	
	void send_string(string);
	void send_code(int, string);
	void send_multicode(int, string);
	void handle_command(map<string, cmdfunc>*, string, string);
	void handle_data(void);
	int data_start(func, short events);
	void data_end(void);
};

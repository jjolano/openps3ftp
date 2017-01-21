#pragma once

#include <string>
#include <vector>
#include <map>

#include <net/net.h>

class Client;

#include "command.h"

using namespace std;

typedef int (*func)(Client*);

class Client {
private:
    func data_handler;
    vector<pollfd>* pollfds;
    map<int, int>* clients_data;

public:
    int socket_ctrl;
    int socket_data;
    int socket_pasv;
    char* buffer;
    char* buffer_data;
    string lastcmd;

    bool cvar_auth;
    string cvar_user;
    string cvar_cwd;
    string cvar_rnfr;
    u64 cvar_rest;
    s32 cvar_fd;

    Client(int, vector<pollfd>*, map<int, int>*);
    void send_string(string);
    void send_code(int, string);
    void send_multicode(int, string);
    void handle_command(map<string, cmdfunc>*, string, string);
    void handle_data(void);
    int data_start(func, short events);
    void data_end(void);
};

#pragma once

#include <string>

// data handler function pointer type
typedef void (*datahnd)(int sock_data);

struct ftp_client {
	int sock_control;
	int sock_data;
	int sock_pasv;
	datahnd data_handler;

	void control_sendCustom(std::string message);
	void control_sendCode(unsigned int code, std::string message, bool multi);
	void control_sendCode(unsigned int code, std::string message);

	bool data_open(datahnd handler, short events);
	int data_send(const char* data, int bytes);
	int data_recv(char* data, int bytes);
	void data_close();
};

// command handler function pointer type
typedef void (*cmdhnd)(ftp_client* clnt, std::string cmd, std::string args);

// libnet/socket.c
extern "C" {
	int closesocket(int socket);
}

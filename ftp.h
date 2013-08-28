#pragma once

#include <string>

struct ftp_client {
	int sock_control;
	int sock_data;
	int sock_pasv;

	void control_sendCustom(std::string message);
	void control_sendCode(unsigned int code, std::string message, bool multi);
	void control_sendCode(unsigned int code, std::string message);

	bool data_open(void (*handler)(ftp_client* clnt), short events);
	void data_close();
};

// command handler function pointer type
typedef void (*cmdhnd)(ftp_client* clnt, std::string cmd, std::string args);

#pragma once

#include <string>
#include <map>

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
	void data_close();
};

// command handler function pointer type
typedef void (*cmdhnd)(ftp_client* clnt, std::string cmd, std::string args);

// various types
typedef std::map<std::string,cmdhnd> ftp_chnds;

// Missing PSL1GHT function in header
#ifdef __cplusplus
extern "C" {
#endif
	int closesocket(int socket);
#ifdef __cplusplus
}
#endif

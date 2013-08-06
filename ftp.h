#ifndef OPF_FTP_H
#define OPF_FTP_H

#define FTP_DATA_EVENT_SEND	1
#define FTP_DATA_EVENT_RECV	2

#include <string>

struct ftp_client {
	int fd;
	std::string last_cmd;
	void response(unsigned int code, std::string message, bool multi);
	void response(unsigned int code, std::string message);
	void cresponse(std::string message);
};

// data connection handler callback
typedef void (*datahandler)(ftp_client* clnt, int data_fd);

// used for data connections
void register_data_handler(ftp_client* clnt, int data_fd, datahandler data_handler, int event);

// basically closesocket from libnet/socket.c
void sock_close(int socket);

extern "C" {
	int closesocket(int socket);
}

#endif /* OPF_FTP_H */

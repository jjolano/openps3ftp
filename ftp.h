#ifndef OPF_FTP_H
#define OPF_FTP_H

#define FTP_DATA_EVENT_SEND	1
#define FTP_DATA_EVENT_RECV	2

#include <map>
#include <string>

#include <net/net.h>
#include <net/poll.h>

class ftp_client {
public:
	int fd;
	std::string last_cmd;
	void response(unsigned int code, std::string message);
	void multiresponse(unsigned int code, std::string message);
	void custresponse(std::string message);
};

typedef void (*datahandler)(ftp_client* clnt, int data_fd);

void ftp_main(void *arg);
void register_data_handler(int data_fd, datahandler data_handler, int event);

void sock_close(int socket);

#endif /* OPF_FTP_H */

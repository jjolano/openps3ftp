#ifndef OPF_FTP_H
#define OPF_FTP_H

#include <string>

#include <net/poll.h>

typedef struct {
	void response(unsigned int code, std::string message);
} ftp_client;

void ftp_main(void *arg);

#endif /* OPF_FTP_H */

#ifndef OPF_FTP_H
#define OPF_FTP_H

#define OFTP_DATA_BUFSIZE	32768
#define OFTP_LISTEN_BACKLOG	32

// macros
#define NIPQUAD(addr) ((unsigned char *)&addr)[0], ((unsigned char *)&addr)[1], ((unsigned char *)&addr)[2], ((unsigned char *)&addr)[3]
#define FD(socket) ((socket)&~SOCKET_FD_MASK)

void fsthread(void *unused);

#endif /* OPF_FTP_H */

/* ftp.cpp: FTP linker helper. */

#include "ftp.hpp"
#include "server.hpp"

void ftp_server_start(void* server_ptr)
{
	FTP::Server* server = (FTP::Server*) server_ptr;
	server->start();
}

void ftp_server_start_ex(uint64_t server_ptr)
{
	ftp_server_start((void*) ((intptr_t) server_ptr));
}
#include <iostream>
#include <map>
#include <vector>

#include <ppu-types.h>
#include <lv2/sysfs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/poll.h>
#include <netdb.h>

#include "ftp.h"
#include "opf.h"

using namespace std;

map<pollfd,pollfd> pfd_pasv;

void fsthread(void *unused)
{
	vector<pollfd> pfd;
}

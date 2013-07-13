/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jjolano@me.com> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return John Olano
 * ----------------------------------------------------------------------------
 */

#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "ftp.h"
#include "opf.h"
#include "ftpcmd.h"

using namespace std;

void cmd_generic_success(ftp_client* clnt, string cmd, string args)
{
	ostringstream out;
	out << cmd << " command successful";
	clnt->response(1, "");
}

void register_ftp_cmds(ftpcmd_handler* m)
{
	// m->insert(make_pair("CMD", &function));
	m->insert(make_pair("NOOP", &cmd_generic_success));
}

#ifndef OPF_FTPCMD_H
#define OPF_FTPCMD_H

#include <map>
#include <string>

#include "ftp.h"

typedef void (*ftpcmd)(ftp_client* clnt, std::string cmd, std::string args);
typedef std::map<std::string, ftpcmd> ftpcmd_handler;

void register_ftp_cmds(ftpcmd_handler* m);

#endif /* OPF_FTPCMD_H */

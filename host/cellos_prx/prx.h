#pragma once

#include <stdbool.h>
#include <inttypes.h>

#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>

#include "server/server.h"
#include "server/client.h"
#include "server/cmd.h"

#include "commands/base.h"
#include "commands/ext.h"
#include "commands/feat.h"
#include "commands/site.h"

#include "compat/cellos_prx/vsh_exports.h"

inline void _sys_ppu_thread_exit(uint64_t val);
void finalize_module(void);
void prx_unload(void);
int prx_stop(void);
int prx_exit(void);

void ftp_stop(void);
void ftp_main(uint64_t ptr);

void prx_main(uint64_t ptr);
int prx_start(size_t args, void* argv);

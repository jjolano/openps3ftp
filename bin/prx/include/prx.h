#pragma once

#include <stdbool.h>
#include <inttypes.h>

#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>

#include "server.h"
#include "command.h"

#include "feat/feat.h"
#include "base/base.h"
#include "ext/ext.h"

#include "vsh_exports.h"

inline void _sys_ppu_thread_exit(uint64_t val);
void finalize_module(void);
int prx_stop(void);

void ftp_stop(void);
void ftp_main(uint64_t ptr);

void prx_main(uint64_t ptr);
int prx_start(size_t args, void* argv);

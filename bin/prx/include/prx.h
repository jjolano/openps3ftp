#pragma once

#define _OPENPS3FTP_STRUCTS_

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

void* getNIDfunc(const char* vsh_module, uint32_t fnid, int32_t offset);
void show_msg(const char* msg);
inline void _sys_ppu_thread_exit(uint64_t val);
void finalize_module(void);
void prx_unload(void);
int prx_stop(void);
int prx_exit(void);
void prx_main(uint64_t ptr);
int prx_start(size_t args, void* argv);

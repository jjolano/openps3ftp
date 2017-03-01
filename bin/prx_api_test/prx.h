#pragma once

#include <stdbool.h>
#include <inttypes.h>

#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>

#include "vsh_exports.h"

inline void _sys_ppu_thread_exit(uint64_t val);
inline sys_prx_id_t prx_get_module_id_by_name(const char* name);
inline sys_prx_id_t prx_get_module_id_by_address(void* addr);
void finalize_module(void);
void prx_unload(void);
int prx_stop(void);
int prx_exit(void);
void prx_main(uint64_t ptr);
int prx_start(size_t args, void* argv);

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>

#include "server.h"
#include "command.h"

#include "feat/feat.h"
#include "base/base.h"
#include "ext/ext.h"

#include "vsh_exports/vsh_exports.h"

void prxmb_if_action(const char* action);

extern uint32_t paf_0A1DC401(int view, int interface, void* handler); // plugin_setInterface
#define plugin_setInterface paf_0A1DC401

extern uint32_t paf_3F7CB0BF(int view, int interface, void* handler); // plugin_setInterface2
#define plugin_setInterface2 paf_3F7CB0BF

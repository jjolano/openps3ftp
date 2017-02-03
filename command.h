#pragma once

#include <string>
#include <map>

#include "types.h"

using namespace std;

#define register_cmd(a,b,c) a->insert(make_pair(b,c));

void register_cmds(map<string, cmd_callback>*);

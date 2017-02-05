#pragma once

#include <string>

class Client;

using namespace std;

typedef int (*data_callback)(Client*);
typedef void (*cmd_callback)(Client*, string);

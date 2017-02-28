#pragma once

struct Client;
struct Server;
struct Command;

typedef bool (*data_callback)(struct Client*);
typedef void (*command_callback)(struct Client*, const char[32], const char*);
typedef void (*connect_callback)(struct Client*);
typedef void (*disconnect_callback)(struct Client*);

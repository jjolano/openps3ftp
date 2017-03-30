#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "server.h"
#include "client.h"

struct ClientNode** search_subtree(struct ClientNode** n, int x);
bool exists_in_subtree(struct ClientNode const* n, int x);
bool exists_in_tree(struct ServerClients const* t, int x);
int height(struct ClientNode const* n);
int balance_factor(struct ClientNode const* n);
struct ClientNode* rebalance(struct ClientNode* n);
struct ClientNode* insert_in_subtree(struct ClientNode* n, int x, struct Client* client);
void insert_node(struct ServerClients* t, int x, struct Client* client);
struct ClientNode* immediate_predecessor(struct ClientNode* n);
struct ClientNode* remove_node(struct ClientNode* n);
struct ClientNode* remove_from_subtree(struct ClientNode* n, int x);
void remove_from_tree(struct ServerClients* t, int x);
void destroy_subtree(struct ClientNode* n);
void destroy_tree(struct ServerClients* t);

#ifdef __cplusplus
}
#endif

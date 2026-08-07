/*
 * Legacy command registry shim.
 *
 * The legacy Command struct keeps its PTTree of callbacks (command_call
 * semantics unchanged). command_register additionally records the
 * registration in a shim-side registry; when the core server starts,
 * the after_commands hook installs one core dispatch adapter per legacy
 * command, so legacy registrations take precedence over core defaults.
 */
#include "shim.h"

#include <stdlib.h>
#include <string.h>

/* One registration: the PTTree entry + the core adapter context. */
#define SHIM_MAX_CMDS 128

struct shim_cmdreg {
    struct Command* cmd;
    char name[32];
    bool used;
};

static struct shim_cmdreg shim_cmds[SHIM_MAX_CMDS];

void command_call_connect(struct Command* command, struct Client* client)
{
    if (command->connect_callbacks != NULL)
    {
        int i;
        for(i = 0; i < command->num_connect_callbacks; ++i)
        {
            struct ConnectCallback* cb = &command->connect_callbacks[i];

            if(*cb->callback != NULL)
            {
                (*cb->callback)(client);
            }
        }
    }
}

void command_call_disconnect(struct Command* command, struct Client* client)
{
    if (command->disconnect_callbacks != NULL)
    {
        int i;
        for(i = 0; i < command->num_disconnect_callbacks; ++i)
        {
            struct DisconnectCallback* cb = &command->disconnect_callbacks[i];

            if(*cb->callback != NULL)
            {
                (*cb->callback)(client);
            }
        }
    }
}

void command_register_connect(struct Command* command, connect_callback callback)
{
    command->connect_callbacks = (struct ConnectCallback*) realloc(command->connect_callbacks, ++command->num_connect_callbacks * sizeof(struct ConnectCallback));

    struct ConnectCallback* cb = &command->connect_callbacks[command->num_connect_callbacks - 1];

    cb->callback = callback;
}

void command_register_disconnect(struct Command* command, disconnect_callback callback)
{
    command->disconnect_callbacks = (struct DisconnectCallback*) realloc(command->disconnect_callbacks, ++command->num_disconnect_callbacks * sizeof(struct DisconnectCallback));

    struct DisconnectCallback* cb = &command->disconnect_callbacks[command->num_disconnect_callbacks - 1];

    cb->callback = callback;
}

bool command_call(struct Command* command, const char name[32], const char* param, struct Client* client)
{
    struct PTNode* n = pttree_search(command->command_callbacks, name);

    if(n != NULL)
    {
        if(n->data_ptr != NULL)
        {
            command_callback callback = n->data_ptr;
            (*callback)(client, name, param);
            return true;
        }
    }

    return false;
}

void command_register(struct Command* command, const char name[32], command_callback callback)
{
    pttree_insert(command->command_callbacks, name, callback);

    /* record for the core-dispatch overlay (after_commands hook) */
    for (int i = 0; i < SHIM_MAX_CMDS; i++) {
        if (shim_cmds[i].used && shim_cmds[i].cmd == command &&
            strcmp(shim_cmds[i].name, name) == 0)
            return;                 /* already recorded */
    }
    for (int i = 0; i < SHIM_MAX_CMDS; i++) {
        if (!shim_cmds[i].used) {
            shim_cmds[i].cmd = command;
            snprintf(shim_cmds[i].name, sizeof(shim_cmds[i].name), "%s", name);
            shim_cmds[i].used = true;
            return;
        }
    }
}

void command_unregister(struct Command* command, const char name[32])
{
    struct PTNode* n = pttree_search(command->command_callbacks, name);

    if(n != NULL)
    {
        n->data_ptr = NULL;
    }
}

void command_init(struct Command* command)
{
    command->command_callbacks = pttree_create();

    command->connect_callbacks = NULL;
    command->num_connect_callbacks = 0;

    command->disconnect_callbacks = NULL;
    command->num_disconnect_callbacks = 0;
}

void command_free(struct Command* command)
{
    if(command->command_callbacks != NULL)
    {
        pttree_destroy(command->command_callbacks);
    }

    if(command->connect_callbacks != NULL)
    {
        free(command->connect_callbacks);
    }

    if(command->disconnect_callbacks != NULL)
    {
        free(command->disconnect_callbacks);
    }

    /* drop stale registrations for this command */
    for (int i = 0; i < SHIM_MAX_CMDS; i++)
        if (shim_cmds[i].used && shim_cmds[i].cmd == command)
            shim_cmds[i].used = false;
}

/* ---- core dispatch adapter (installed by the after_commands hook) ---- */

static void legacy_dispatch_adapter(struct opftp_client* c,
                                    const char* param, void* ctx)
{
    struct shim_cmdreg* reg = ctx;
    struct Client* facade = (struct Client*) opftp_client_userdata(c);
    if (!facade)
        return;
    if (!command_call(reg->cmd, reg->name, param, facade)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Command not implemented.");
        opftp_client_send_reply(c, 502, buf);
    } else {
        strcpy(facade->lastcmd, reg->name);
    }
}

/* Called by the core reactor (after_commands hook) once the default
 * command table exists; legacy handlers override the core defaults. */
void legacy_command_overlay(struct opftp_server* core, struct Command* cmd)
{
    if (!core || !cmd)
        return;
    for (int i = 0; i < SHIM_MAX_CMDS; i++) {
        if (shim_cmds[i].used && shim_cmds[i].cmd == cmd)
            opftp_dispatch_register(core, shim_cmds[i].name,
                                    legacy_dispatch_adapter, &shim_cmds[i]);
    }
}

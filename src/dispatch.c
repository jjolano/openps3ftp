/*
 * Command registry: hash map from FTP command name to handler.
 * Open addressing, fixed capacity (FTP command space is tiny).
 */
#include "opftp.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DISPATCH_CAP 64

static unsigned hash_name(const char* name)
{
    unsigned h = 5381;
    while (*name)
        h = h * 33 + (unsigned char) *name++;
    return h;
}

void opftp_dispatch_init(struct opftp_server* s)
{
    if (s->dispatch_entries)
        return;
    s->dispatch_entries = calloc(DISPATCH_CAP, sizeof(*s->dispatch_entries));
    s->dispatch_cap = DISPATCH_CAP;
}

void opftp_dispatch_free(struct opftp_server* s)
{
    free(s->dispatch_entries);
    s->dispatch_entries = NULL;
    s->dispatch_cap = 0;
}

int opftp_dispatch_register(struct opftp_server* s, const char* name, opftp_cmd_fn fn, void* ctx)
{
    if (!s->dispatch_entries || !name || !fn)
        return -EINVAL;
    char up[8];
    size_t len = strlen(name);
    if (len == 0 || len >= sizeof(up))
        return -EINVAL;
    for (size_t i = 0; i < len; i++)
        up[i] = (char) toupper((unsigned char) name[i]);
    up[len] = '\0';

    unsigned slot = hash_name(up) % s->dispatch_cap;
    for (unsigned probe = 0; probe < s->dispatch_cap; probe++) {
        unsigned i = (slot + probe) % s->dispatch_cap;
        if (!s->dispatch_entries[i].used) {
            memcpy(s->dispatch_entries[i].name, up, len + 1);
            s->dispatch_entries[i].fn = fn;
            s->dispatch_entries[i].ctx = ctx;
            s->dispatch_entries[i].used = true;
            return 0;
        }
        if (strcmp(s->dispatch_entries[i].name, up) == 0) {
            /* replace existing handler */
            s->dispatch_entries[i].fn = fn;
            s->dispatch_entries[i].ctx = ctx;
            return 0;
        }
    }
    return -ENOSPC;
}

int opftp_dispatch_unregister(struct opftp_server* s, const char* name)
{
    if (!s->dispatch_entries || !name)
        return -EINVAL;
    unsigned slot = hash_name(name) % s->dispatch_cap;
    for (unsigned probe = 0; probe < s->dispatch_cap; probe++) {
        unsigned i = (slot + probe) % s->dispatch_cap;
        if (!s->dispatch_entries[i].used)
            return -ENOENT;
        if (strcmp(s->dispatch_entries[i].name, name) == 0) {
            s->dispatch_entries[i].used = false;
            return 0;
        }
    }
    return -ENOENT;
}

int opftp_dispatch_call(struct opftp_server* s, struct opftp_client* c,
                        const char* name, const char* param)
{
    if (!s->dispatch_entries || !name)
        return 1;
    unsigned slot = hash_name(name) % s->dispatch_cap;
    for (unsigned probe = 0; probe < s->dispatch_cap; probe++) {
        unsigned i = (slot + probe) % s->dispatch_cap;
        if (!s->dispatch_entries[i].used)
            return 1;
        if (strcmp(s->dispatch_entries[i].name, name) == 0) {
            s->dispatch_entries[i].fn(c, param, s->dispatch_entries[i].ctx);
            return 0;
        }
    }
    return 1;
}

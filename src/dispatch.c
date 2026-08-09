/*
 * Command registry: flat array of {name, fn, ctx} + linear strcmp scan.
 * FTP command count is ~50 (fixed, registered once in commands.c).
 */
#include "opftp.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DISPATCH_CAP 64

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

    /* find existing slot for this name, or first unused slot */
    int first_unused = -1;
    for (unsigned i = 0; i < s->dispatch_cap; i++) {
        if (!s->dispatch_entries[i].used) {
            if (first_unused < 0)
                first_unused = (int) i;
            continue;
        }
        if (strcmp(s->dispatch_entries[i].name, up) == 0) {
            /* replace existing handler */
            s->dispatch_entries[i].fn = fn;
            s->dispatch_entries[i].ctx = ctx;
            return 0;
        }
    }
    if (first_unused < 0)
        return -ENOSPC;
    memcpy(s->dispatch_entries[first_unused].name, up, len + 1);
    s->dispatch_entries[first_unused].fn = fn;
    s->dispatch_entries[first_unused].ctx = ctx;
    s->dispatch_entries[first_unused].used = true;
    return 0;
}

int opftp_dispatch_call(struct opftp_server* s, struct opftp_client* c,
                        const char* name, const char* param)
{
    if (!s->dispatch_entries || !name)
        return 1;
    for (unsigned i = 0; i < s->dispatch_cap; i++) {
        if (!s->dispatch_entries[i].used)
            continue;
        if (strcmp(s->dispatch_entries[i].name, name) == 0) {
            s->dispatch_entries[i].fn(c, param, s->dispatch_entries[i].ctx);
            return 0;
        }
    }
    return 1;
}
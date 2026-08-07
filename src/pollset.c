/*
 * opftp pollset — poll multiplexer with self-pipe wake.
 *
 * Handles are stable: entries are never moved (no swap-last), so a
 * handle remains valid for the lifetime of its fd. Removed entries
 * are marked fd=-1 and their slots are reused by future adds.
 * fd 0 is the internal wake pipe.
 */
#include "opftp.h"
#ifdef OPFTP_PS3
#include <net/poll.h>
#else
#include <poll.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

struct opftp_pollset {
    struct pollfd* fds;
    void** users;
    int cap, count;          /* count = number of slots in use (incl. wake) */
    int wake_r, wake_w;
};

opftp_pollset_t* opftp_pollset_create(void)
{
    opftp_pollset_t* p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->cap = 8;
    p->fds = calloc(p->cap, sizeof(*p->fds));
    p->users = calloc(p->cap, sizeof(*p->users));
    if (!p->fds || !p->users) { free(p->fds); free(p->users); free(p); return NULL; }

#ifndef OPFTP_PS3
    /* PS3 poll() only watches sockets — a pipe cannot be polled there.
     * The host keeps an internal wake pipe for instant wakes; the ps3
     * reactor simply polls with a 1s timeout (completions and stop are
     * picked up within a second). */
    int pipefd[2];
    if (pipe(pipefd) != 0) { free(p->fds); free(p->users); free(p); return NULL; }
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    p->wake_r = pipefd[0];
    p->wake_w = pipefd[1];

    /* slot 0: wake pipe */
    p->fds[0].fd = pipefd[0];
    p->fds[0].events = POLLIN;
    p->count = 1;
#endif
    return p;
}

void opftp_pollset_destroy(opftp_pollset_t* p)
{
    if (!p) return;
#ifndef OPFTP_PS3
    close(p->wake_r);
    close(p->wake_w);
#endif
    free(p->fds);
    free(p->users);
    free(p);
}

static int ensure_cap(opftp_pollset_t* p, int need)
{
    if (need <= p->cap) return 0;
    int ncap = p->cap * 2;
    struct pollfd* nf = realloc(p->fds, ncap * sizeof(*nf));
    void** nu = realloc(p->users, ncap * sizeof(*nu));
    if (!nf || !nu) { free(nf); free(nu); return -1; }
    p->fds = nf;
    p->users = nu;
    p->cap = ncap;
    return 0;
}

int opftp_pollset_add(opftp_pollset_t* p, int fd, short events, void* user)
{
    if (!p || fd < 0) return -1;
    /* reuse a freed slot if any (stable handles: prefer the first) */
#ifndef OPFTP_PS3
    for (int i = 1; i < p->count; i++) {
#else
    for (int i = 0; i < p->count; i++) {
#endif
        if (p->fds[i].fd < 0) {
            p->fds[i].fd = fd;
            p->fds[i].events = events;
            p->fds[i].revents = 0;
            p->users[i] = user;
            return i;
        }
    }
    if (ensure_cap(p, p->count + 1) != 0) return -1;
    int h = p->count++;
#ifdef OPFTP_PS3
    if (h == 0) h = p->count++;   /* no wake slot on ps3 */
#endif
    p->fds[h].fd = fd;
    p->fds[h].events = events;
    p->fds[h].revents = 0;
    p->users[h] = user;
    return h;
}

void opftp_pollset_mod(opftp_pollset_t* p, int handle, short events)
{
    if (!p || handle < 0 || handle >= p->count) return;
#ifndef OPFTP_PS3
    if (handle == 0) return;   /* wake slot */
#endif
    p->fds[handle].events = events;
}

/* Mark the slot free. The handle is never reused for a different fd
 * while this entry is live, and removed slots are reused by add. */
void opftp_pollset_remove(opftp_pollset_t* p, int handle)
{
    if (!p || handle < 0 || handle >= p->count) return;
#ifndef OPFTP_PS3
    if (handle == 0) return;   /* wake slot */
#endif
    p->fds[handle].fd = -1;
    p->fds[handle].revents = 0;
    p->users[handle] = NULL;
}

int opftp_pollset_wait(opftp_pollset_t* p, int timeout_ms)
{
    if (!p) return -1;
    int r = poll(p->fds, p->count, timeout_ms);
    if (r <= 0) return r;
    int n = 0;
    for (int i = 0; i < p->count; i++) {
#ifndef OPFTP_PS3
        if (i == 0 && (p->fds[0].revents & (POLLIN | POLLERR))) {
            /* drain wake pipe */
            char buf[64];
            while (read(p->wake_r, buf, sizeof(buf)) > 0) { }
            p->fds[0].revents = 0;
            continue;
        }
#endif
        if (p->fds[i].fd >= 0 && p->fds[i].revents != 0)
            n++;
    }
    return n;
}

void opftp_pollset_wake(opftp_pollset_t* p)
{
#ifndef OPFTP_PS3
    if (!p) return;
    char c = 1;
    ssize_t r = write(p->wake_w, &c, 1);
    (void) r;
#else
    (void) p;   /* reactor polls with a 1s timeout instead */
#endif
}

/* One pass after wait(): copy the (user, revents) pairs of every
 * ready slot into caller arrays (bounded by max). Returns the count.
 * Iterating the copy avoids the O(n) rescan-per-event of the old
 * accessors AND the index-shift wart when a handler disconnects a
 * client mid-iteration (the copy is stable). */
int opftp_pollset_collect(opftp_pollset_t* p, void** users, short* events,
                          int max)
{
    if (!p || !users || !events || max <= 0)
        return 0;
    int n = 0;
    for (int j = 0; j < p->count && n < max; j++) {
#ifndef OPFTP_PS3
        if (j == 0) continue;   /* wake slot */
#endif
        if (p->fds[j].fd >= 0 && p->fds[j].revents != 0) {
            users[n] = p->users[j];
            events[n] = p->fds[j].revents;
            n++;
        }
    }
    return n;
}

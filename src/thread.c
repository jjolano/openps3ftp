/*
 * opftp thread portability layer (host: pthread).
 * Opaque-handle API mirrors the legacy sys.thread shape so the compat
 * shim can map onto it; P4 swaps the implementation for ps3.
 */
#include "opftp.h"
#include <stdlib.h>
#include <string.h>

#ifdef OPFTP_PS3
/* ---- PS3: lv2 mutex/cond/thread syscalls ---- */

#include <sys/mutex.h>
#include <sys/cond.h>
#include <sys/ppu_thread.h>

void* opftp_mutex_create(void)
{
    sys_mutex_t* m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    sys_mutex_attr_t attr;
    sysMutexAttrInitialize(attr);
    if (sysMutexCreate(m, &attr) != 0) { free(m); return NULL; }
    return m;
}

int opftp_mutex_lock(void* m)   { return sysMutexLock(*(sys_mutex_t*) m, 0); }
int opftp_mutex_unlock(void* m) { return sysMutexUnlock(*(sys_mutex_t*) m); }

int opftp_mutex_destroy(void* m)
{
    if (!m) return -1;
    int r = sysMutexDestroy(*(sys_mutex_t*) m);
    free(m);
    return r;
}

void* opftp_cond_create(void* m)
{
    if (!m) return NULL;
    sys_cond_t* c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    sys_cond_attr_t attr;
    sysCondAttrInitialize(attr);
    /* ps3 binds the mutex at create time */
    if (sysCondCreate(c, *(sys_mutex_t*) m, &attr) != 0) { free(c); return NULL; }
    return c;
}

int opftp_cond_wait(void* c, void* m)
{
    (void) m;   /* mutex is bound at create; the caller must hold it */
    return sysCondWait(*(sys_cond_t*) c, 0);
}

int opftp_cond_signal(void* c)    { return sysCondSignal(*(sys_cond_t*) c); }
int opftp_cond_broadcast(void* c) { return sysCondBroadcast(*(sys_cond_t*) c); }

int opftp_cond_destroy(void* c)
{
    if (!c) return -1;
    int r = sysCondDestroy(*(sys_cond_t*) c);
    free(c);
    return r;
}

struct opftp_thread { sys_ppu_thread_t t; };

struct start_args { void (*fn)(void*); void* arg; };

static void ps3_thread_entry(uint64_t arg)
{
    struct start_args* sa = (struct start_args*) (uintptr_t) arg;
    sa->fn(sa->arg);
    free(sa);
    sys_ppu_thread_exit(0);
}

void* opftp_thread_create(void (*fn)(void*), void* arg)
{
    struct opftp_thread* wt = calloc(1, sizeof(*wt));
    struct start_args* sa = malloc(sizeof(*sa));
    if (!wt || !sa) { free(wt); free(sa); return NULL; }
    sa->fn = fn;
    sa->arg = arg;
    if (sys_ppu_thread_create(&wt->t, ps3_thread_entry,
                             (uint64_t) (uintptr_t) sa, 1001, 128 * 1024,
                             SYS_PPU_THREAD_CREATE_JOINABLE, "opftp") != 0) {
        free(wt);
        free(sa);
        return NULL;
    }
    return wt;
}

void* opftp_thread_join(void* t)
{
    struct opftp_thread* wt = t;
    if (wt) {
        uint64_t ret = 0;
        sys_ppu_thread_join(wt->t, &ret);
    }
    return NULL;
}

void opftp_thread_destroy(void* t)
{
    free(t);
}

#else /* host: pthread */

#include <pthread.h>

void* opftp_mutex_create(void)
{
    pthread_mutex_t* m = calloc(1, sizeof(*m));
    if (m)
        pthread_mutex_init(m, NULL);
    return m;
}

int opftp_mutex_lock(void* m)   { return pthread_mutex_lock((pthread_mutex_t*) m); }
int opftp_mutex_unlock(void* m) { return pthread_mutex_unlock((pthread_mutex_t*) m); }

int opftp_mutex_destroy(void* m)
{
    pthread_mutex_t* pm = m;
    if (!pm) return -1;
    int r = pthread_mutex_destroy(pm);
    free(pm);
    return r;
}

void* opftp_cond_create(void* m)
{
    (void) m;
    pthread_cond_t* c = calloc(1, sizeof(*c));
    if (c)
        pthread_cond_init(c, NULL);
    return c;
}

int opftp_cond_wait(void* c, void* m)
{
    return pthread_cond_wait((pthread_cond_t*) c, (pthread_mutex_t*) m);
}

int opftp_cond_signal(void* c)    { return pthread_cond_signal((pthread_cond_t*) c); }
int opftp_cond_broadcast(void* c) { return pthread_cond_broadcast((pthread_cond_t*) c); }

int opftp_cond_destroy(void* c)
{
    pthread_cond_t* pc = c;
    if (!pc) return -1;
    int r = pthread_cond_destroy(pc);
    free(pc);
    return r;
}

/* ---- threads ---- */

struct opftp_thread {
    pthread_t t;
};

struct start_args {
    void (*fn)(void*);
    void* arg;
};

static void* opftp_thread_entry(void* arg)
{
    struct start_args* sa = arg;
    sa->fn(sa->arg);
    free(sa);
    return NULL;
}

void* opftp_thread_create(void (*fn)(void*), void* arg)
{
    struct opftp_thread* wt = calloc(1, sizeof(*wt));
    struct start_args* sa = malloc(sizeof(*sa));
    if (!wt || !sa) { free(wt); free(sa); return NULL; }
    sa->fn = fn;
    sa->arg = arg;
    if (pthread_create(&wt->t, NULL, opftp_thread_entry, sa) != 0) {
        free(wt); free(sa);
        return NULL;
    }
    return wt;
}

/* Thread fn is void(*)(void*); join waits and returns NULL. */
void* opftp_thread_join(void* t)
{
    struct opftp_thread* wt = t;
    if (wt)
        pthread_join(wt->t, NULL);
    return NULL;
}

void opftp_thread_destroy(void* t)
{
    free(t);
}

#endif /* OPFTP_PS3 */

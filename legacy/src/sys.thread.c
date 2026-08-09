#include <stdlib.h>
#ifdef LINUX
#include <time.h>
#endif

#ifdef PSL1GHT_SDK
#include <sys/mutex.h>
#include <sys/cond.h>
#include <sys/thread.h>
#ifndef SYS_MUTEX_ATTR_PSHARED
#define SYS_MUTEX_ATTR_PSHARED 0x0100
#endif
#endif

#ifdef LINUX
#include <pthread.h>
#include <sched.h>
#endif

#include "common.h"
#include "sys.thread.h"

struct _sys_thread_data {
	void (*func)(void*);
	void* arg;
};

#ifdef PSL1GHT_SDK
inline void* sys_thread_mutex_alloc(int num)
{
	return malloc(sizeof(sys_mutex_t) * num);
}

inline int sys_thread_mutex_create(void* ptr_mutex)
{
	sys_mutex_attr_t attr;

	attr.attr_protocol = SYS_MUTEX_PROTOCOL_FIFO;
	attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
	attr.attr_pshared = SYS_MUTEX_ATTR_PSHARED;
	attr.attr_adaptive = SYS_MUTEX_ATTR_ADAPTIVE;

	attr.key = 0;
	attr.flags = 0;
	attr.name[0] = '\0';

	return sysMutexCreate((sys_mutex_t*) ptr_mutex, &attr);
}

inline int sys_thread_mutex_lock(void* ptr_mutex)
{
	return sysMutexLock(*((sys_mutex_t*) ptr_mutex), 0);
}

inline int sys_thread_mutex_trylock(void* ptr_mutex)
{
	return sysMutexTryLock(*((sys_mutex_t*) ptr_mutex));
}

inline int sys_thread_mutex_unlock(void* ptr_mutex)
{
	return sysMutexUnlock(*((sys_mutex_t*) ptr_mutex));
}

inline int sys_thread_mutex_destroy(void* ptr_mutex)
{
	return sysMutexDestroy(*((sys_mutex_t*) ptr_mutex));
}

inline void sys_thread_mutex_free(void* ptr_mutex)
{
	free((sys_mutex_t*) ptr_mutex);
}

inline void* sys_thread_cond_alloc(int num)
{
	return malloc(sizeof(sys_cond_t) * num);
}

inline int sys_thread_cond_create(void* ptr_cond, void* ptr_mutex)
{
	sys_cond_attr_t attr;
	
	attr.attr_pshared = SYS_COND_ATTR_PSHARED;
	attr.flags = 0;
	attr.key = 0;
	attr.name[0] = '\0';

	return sysCondCreate((sys_cond_t*) ptr_cond, *((sys_mutex_t*) ptr_mutex), &attr);
}

inline int sys_thread_cond_wait(void* ptr_cond, void* ptr_mutex)
{
	return sysCondWait(*((sys_cond_t*) ptr_cond), 0);
}

inline int sys_thread_cond_timedwait(void* ptr_cond, void* ptr_mutex, int timeout_ms)
{
	(void) ptr_mutex;
	return sysCondWait(*((sys_cond_t*) ptr_cond), (uint64_t) timeout_ms * 1000);
}

inline int sys_thread_cond_signal(void* ptr_cond)
{
	return sysCondSignal(*((sys_cond_t*) ptr_cond));
}

inline int sys_thread_cond_broadcast(void* ptr_cond)
{
	return sysCondBroadcast(*((sys_cond_t*) ptr_cond));
}

inline int sys_thread_cond_destroy(void* ptr_cond)
{
	return sysCondDestroy(*((sys_cond_t*) ptr_cond));
}

inline void sys_thread_cond_free(void* ptr_cond)
{
	free((sys_cond_t*) ptr_cond);
}

inline void* sys_thread_alloc(int num)
{
	return malloc(sizeof(sys_ppu_thread_t) * num);
}

inline int sys_thread_create(void* ptr_thread, void (*func)(void*), void* arg)
{
	return sysThreadCreate((sys_ppu_thread_t*) ptr_thread, func, arg, 1002, 0x2000, THREAD_JOINABLE, "");
}

inline int sys_thread_create2(void* ptr_threads, int index, void (*func)(void*), void* arg)
{
	sys_ppu_thread_t* threads = ptr_threads;
	sys_ppu_thread_t* thread = &threads[index];

	return sys_thread_create(thread, func, arg);
}

inline int sys_thread_join(void* ptr_thread, void** ptr_retval)
{
	sys_ppu_thread_t thread = (*(sys_ppu_thread_t*) ptr_thread);
	return sysThreadJoin(thread, (uint64_t*) *((uint64_t**) ptr_retval));
}

inline int sys_thread_join2(void* ptr_threads, int index, void** ptr_retval)
{
	sys_ppu_thread_t* threads = ptr_threads;
	sys_ppu_thread_t* thread = &threads[index];

	return sys_thread_join(thread, ptr_retval);
}

inline void sys_thread_exit(void* ptr_retval)
{
	return sysThreadExit((uint64_t) ptr_retval);
}

inline void sys_thread_free(void* ptr_thread)
{
	free((sys_ppu_thread_t*) ptr_thread);
}

inline void sys_thread_yield(void)
{
	sysThreadYield();
}
#endif

#ifdef LINUX
void* _sys_thread(void*);

void* _sys_thread(void* arg)
{
	struct _sys_thread_data* data = arg;
	void (*func)(void*) = data->func;
	void* func_arg = data->arg;
	free(data);

	if(func != NULL)
	{
		(*func)(func_arg);
	}

	return NULL;
}

inline void* sys_thread_mutex_alloc(int num)
{
	return malloc(sizeof(pthread_mutex_t) * num);
}

inline int sys_thread_mutex_create(void* ptr_mutex)
{
	return pthread_mutex_init((pthread_mutex_t*) ptr_mutex, NULL);
}

inline int sys_thread_mutex_lock(void* ptr_mutex)
{
	return pthread_mutex_lock((pthread_mutex_t*) ptr_mutex);
}

inline int sys_thread_mutex_trylock(void* ptr_mutex)
{
	return pthread_mutex_trylock((pthread_mutex_t*) ptr_mutex);
}

inline int sys_thread_mutex_unlock(void* ptr_mutex)
{
	return pthread_mutex_unlock((pthread_mutex_t*) ptr_mutex);
}

inline int sys_thread_mutex_destroy(void* ptr_mutex)
{
	return pthread_mutex_destroy((pthread_mutex_t*) ptr_mutex);
}

inline void sys_thread_mutex_free(void* ptr_mutex)
{
	free((pthread_mutex_t*) ptr_mutex);
}

inline void* sys_thread_cond_alloc(int num)
{
	return malloc(sizeof(pthread_cond_t) * num);
}

inline int sys_thread_cond_create(void* ptr_cond, void* ptr_mutex)
{
	return pthread_cond_init((pthread_cond_t*) ptr_cond, NULL);
}

inline int sys_thread_cond_wait(void* ptr_cond, void* ptr_mutex)
{
	return pthread_cond_wait((pthread_cond_t*) ptr_cond, (pthread_mutex_t*) ptr_mutex);
}

inline int sys_thread_cond_timedwait(void* ptr_cond, void* ptr_mutex, int timeout_ms)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
	return pthread_cond_timedwait((pthread_cond_t*) ptr_cond, (pthread_mutex_t*) ptr_mutex, &ts);
}

inline int sys_thread_cond_signal(void* ptr_cond)
{
	return pthread_cond_signal((pthread_cond_t*) ptr_cond);
}

inline int sys_thread_cond_broadcast(void* ptr_cond)
{
	return pthread_cond_broadcast((pthread_cond_t*) ptr_cond);
}

inline int sys_thread_cond_destroy(void* ptr_cond)
{
	return pthread_cond_destroy((pthread_cond_t*) ptr_cond);
}

inline void sys_thread_cond_free(void* ptr_cond)
{
	free((pthread_cond_t*) ptr_cond);
}

inline void* sys_thread_alloc(int num)
{
	return malloc(sizeof(pthread_t) * num);
}

inline int sys_thread_create(void* ptr_thread, void (*func)(void*), void* arg)
{
	struct _sys_thread_data* data = malloc(sizeof(struct _sys_thread_data));
	data->func = func;
	data->arg = arg;

	pthread_attr_t attr;
	pthread_attr_init(&attr);
  	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	return pthread_create((pthread_t*) ptr_thread, &attr, _sys_thread, data);
}

inline int sys_thread_create2(void* ptr_threads, int index, void (*func)(void*), void* arg)
{
	pthread_t* threads = ptr_threads;
	pthread_t* thread = &threads[index];

	return sys_thread_create(thread, func, arg);
}

inline int sys_thread_join(void* ptr_thread, void** ptr_retval)
{
	pthread_t thread = (*(pthread_t*) ptr_thread);
	return pthread_join(thread, ptr_retval);
}

inline int sys_thread_join2(void* ptr_threads, int index, void** ptr_retval)
{
	pthread_t* threads = ptr_threads;
	pthread_t* thread = &threads[index];

	return sys_thread_join(thread, ptr_retval);
}

inline void sys_thread_exit(void* ptr_retval)
{
	return pthread_exit(ptr_retval);
}

inline void sys_thread_free(void* ptr_thread)
{
	free((pthread_t*) ptr_thread);
}

inline void sys_thread_yield(void)
{
	sched_yield();
}
#endif

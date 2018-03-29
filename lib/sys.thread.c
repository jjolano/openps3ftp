#include <stdlib.h>

#ifdef CELL_SDK
#include <sys/synchronization.h>
#include <sys/ppu_thread.h>
#endif

#ifdef LINUX
#include <pthread.h>
#endif

#include "common.h"
#include "sys.thread.h"

struct _sys_thread_data {
	void (*func)(void*);
	void* arg;
};

void* _sys_thread(void*);

void* _sys_thread(void* arg)
{
	struct _sys_thread_data* data = arg;
	(*data->func)(data->arg);
	free(data);

	return NULL;
}

#ifdef CELL_SDK
inline void* sys_thread_mutex_alloc(int num)
{
	return malloc(sizeof(sys_lwmutex_t) * num);
}

inline int sys_thread_mutex_create(void* ptr_mutex)
{
	sys_lwmutex_attribute_t attr;
	//sys_lwmutex_attribute_initialize(attr);
	attr.attr_protocol = SYS_SYNC_FIFO;
	attr.attr_recursive = SYS_SYNC_RECURSIVE;
	attr.name[0] = '\0';

	return sys_lwmutex_create((sys_lwmutex_t*) ptr_mutex, &attr);
}

inline int sys_thread_mutex_lock(void* ptr_mutex)
{
	return sys_lwmutex_lock((sys_lwmutex_t*) ptr_mutex, 0);
}

inline int sys_thread_mutex_trylock(void* ptr_mutex)
{
	return sys_lwmutex_trylock((sys_lwmutex_t*) ptr_mutex);
}

inline int sys_thread_mutex_unlock(void* ptr_mutex)
{
	return sys_lwmutex_unlock((sys_lwmutex_t*) ptr_mutex);
}

inline int sys_thread_mutex_destroy(void* ptr_mutex)
{
	return sys_lwmutex_destroy((sys_lwmutex_t*) ptr_mutex);
}

inline void sys_thread_mutex_free(void* ptr_mutex)
{
	free((sys_lwmutex_t*) ptr_mutex);
}

inline void* sys_thread_cond_alloc(int num)
{
	return malloc(sizeof(sys_lwcond_t) * num);
}

inline int sys_thread_cond_create(void* ptr_cond, void* ptr_mutex)
{
	sys_lwcond_attribute_t attr;
	sys_lwcond_attribute_initialize(attr);

	return sys_lwcond_create((sys_lwcond_t*) ptr_cond, (sys_lwmutex_t*) ptr_mutex, &attr);
}

inline int sys_thread_cond_wait(void* ptr_cond, void* ptr_mutex)
{
	return sys_lwcond_wait((sys_lwcond_t*) ptr_cond, 0);
}

inline int sys_thread_cond_signal(void* ptr_cond)
{
	return sys_lwcond_signal((sys_lwcond_t*) ptr_cond);
}

inline int sys_thread_cond_broadcast(void* ptr_cond)
{
	return sys_lwcond_signal_all((sys_lwcond_t*) ptr_cond);
}

inline int sys_thread_cond_destroy(void* ptr_cond)
{
	return sys_lwcond_destroy((sys_lwcond_t*) ptr_cond);
}

inline void sys_thread_cond_free(void* ptr_cond)
{
	free((sys_lwcond_t*) ptr_cond);
}

inline void* sys_thread_alloc(int num)
{
	return malloc(sizeof(sys_ppu_thread_t) * num);
}

inline int sys_thread_create(void* ptr_thread, void (*func)(void*), void* arg)
{
	return sys_ppu_thread_create((sys_ppu_thread_t*) ptr_thread, func, (uint64_t) arg, 1001, 0x4000, SYS_PPU_THREAD_CREATE_JOINABLE, "");
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
	return sys_ppu_thread_join(thread, (uint64_t) ptr_retval);
}

inline int sys_thread_join2(void* ptr_threads, int index, void** ptr_retval)
{
	sys_ppu_thread_t* threads = ptr_threads;
	sys_ppu_thread_t* thread = &threads[index];

	return sys_thread_join(thread, ptr_retval);
}

inline void sys_thread_exit(void* ptr_retval)
{
	return sys_ppu_thread_exit((uint64_t) ptr_retval);
}

inline void sys_thread_free(void* ptr_thread)
{
	free((sys_ppu_thread_t*) ptr_thread);
}
#endif

#ifdef LINUX
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
#endif

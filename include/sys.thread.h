#ifndef _UTIL_SYS_H_
#define _UTIL_SYS_H_

#ifdef __cplusplus
extern "C" {
#endif

void* sys_thread_mutex_alloc(int num);
int sys_thread_mutex_create(void* ptr_mutex);
int sys_thread_mutex_lock(void* ptr_mutex);
int sys_thread_mutex_trylock(void* ptr_mutex);
int sys_thread_mutex_unlock(void* ptr_mutex);
int sys_thread_mutex_destroy(void* ptr_mutex);
void sys_thread_mutex_free(void* ptr_mutex);

void* sys_thread_cond_alloc(int num);
int sys_thread_cond_create(void* ptr_cond, void* ptr_mutex);
int sys_thread_cond_wait(void* ptr_cond, void* ptr_mutex);
int sys_thread_cond_signal(void* ptr_cond);
int sys_thread_cond_broadcast(void* ptr_cond);
int sys_thread_cond_destroy(void* ptr_cond);
void sys_thread_cond_free(void* ptr_cond);

void* sys_thread_alloc(int num);
int sys_thread_create(void* ptr_thread, void (*func)(void*), void* arg);
int sys_thread_create2(void* ptr_threads, int index, void (*func)(void*), void* arg);
int sys_thread_join(void* ptr_thread, void** ptr_retval);
int sys_thread_join2(void* ptr_threads, int index, void** ptr_retval);
void sys_thread_exit(void* ptr_retval);
void sys_thread_free(void* ptr_thread);
void sys_thread_yield(void);

#ifdef __cplusplus
}
#endif

#endif

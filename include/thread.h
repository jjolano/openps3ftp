#ifndef _UTIL_THREAD_H_
#define _UTIL_THREAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct ThreadPoolJob {
	void (*func)(void*);
	void* arg;
};

struct ThreadPool {
	void* threads;
	int num_threads;

	void* mutex;
	void* cond_job;
	void* cond_get;

	bool running;
	struct ThreadPoolJob job;
};

struct ThreadPool* threadpool_create(int num_threads);
void threadpool_start(struct ThreadPool* pool);
void threadpool_stop(struct ThreadPool* pool);
void threadpool_destroy(struct ThreadPool* pool);

void threadpool_dispatch(struct ThreadPool* pool, void (*func)(void*), void* arg);

#ifdef __cplusplus
}
#endif

#endif

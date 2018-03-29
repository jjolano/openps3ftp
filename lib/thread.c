#include <stdlib.h>
#include "thread.h"
#include "sys.thread.h"

void threadpool_thread(void* arg);

struct ThreadPool* threadpool_create(int num_threads)
{
	if(num_threads > 0)
	{
		struct ThreadPool* pool = malloc(sizeof(struct ThreadPool));
		
		if(pool != NULL)
		{
			pool->threads = sys_thread_alloc(num_threads);
			pool->num_threads = num_threads;
			pool->mutex = sys_thread_mutex_alloc(1);
			pool->cond_job = sys_thread_cond_alloc(1);
			pool->cond_get = sys_thread_cond_alloc(1);

			pool->running = false;
			pool->job.func = NULL;
			pool->job.arg = NULL;

			if(pool->threads == NULL
			|| pool->mutex == NULL
			|| pool->cond_job == NULL
			|| pool->cond_get == NULL
			|| sys_thread_mutex_create(pool->mutex) != 0
			|| sys_thread_cond_create(pool->cond_job, pool->mutex) != 0
			|| sys_thread_cond_create(pool->cond_get, pool->mutex) != 0)
			{
				threadpool_destroy(pool);
				pool = NULL;
			}
		}

		return pool;
	}

	return NULL;
}

void threadpool_start(struct ThreadPool* pool)
{
	if(pool != NULL)
	{
		pool->running = true;

		int i;
		for(i = 0; i < pool->num_threads; i++)
		{
			sys_thread_create2(pool->threads, i, threadpool_thread, pool);
		}
	}
}

void threadpool_stop(struct ThreadPool* pool)
{
	if(pool != NULL)
	{
		pool->running = false;
		sys_thread_cond_broadcast(pool->cond_job);

		int i;
		for(i = 0; i < pool->num_threads; i++)
		{
			sys_thread_join2(pool->threads, i, NULL);
		}
	}
}

void threadpool_destroy(struct ThreadPool* pool)
{
	if(pool != NULL)
	{
		if(pool->threads != NULL)
		{
			sys_thread_free(pool->threads);
		}

		if(pool->mutex != NULL)
		{
			sys_thread_mutex_destroy(pool->mutex);
			sys_thread_mutex_free(pool->mutex);
		}

		if(pool->cond_job != NULL)
		{
			sys_thread_cond_destroy(pool->cond_job);
			sys_thread_cond_free(pool->cond_job);
		}

		if(pool->cond_get != NULL)
		{
			sys_thread_cond_destroy(pool->cond_get);
			sys_thread_cond_free(pool->cond_get);
		}

		free(pool);
	}
}

void threadpool_dispatch(struct ThreadPool* pool, void (*func)(void*), void* arg)
{
	if(pool != NULL)
	{
		if(sys_thread_mutex_lock(pool->mutex) == 0)
		{
			// Set the job.
			pool->job.func = func;
			pool->job.arg = arg;

			// Signal the conditional and unlock the mutex.
			sys_thread_cond_signal(pool->cond_job);

			while(pool->running && pool->job.func != NULL)
			{
				sys_thread_cond_wait(pool->cond_get, pool->mutex);
			}

			sys_thread_mutex_unlock(pool->mutex);
		}
	}
}

void threadpool_thread(void* arg)
{
	struct ThreadPool* pool = arg;

	if(pool == NULL)
	{
		return;
	}

	do
	{
		sys_thread_mutex_lock(pool->mutex);

		while(pool->running && pool->job.func == NULL)
		{
			sys_thread_cond_wait(pool->cond_job, pool->mutex);
		}

		if(!pool->running)
		{
			sys_thread_cond_signal(pool->cond_get);
			sys_thread_mutex_unlock(pool->mutex);
			break;
		}

		struct ThreadPoolJob job = pool->job;

		pool->job.func = NULL;
		pool->job.arg = NULL;

		sys_thread_cond_signal(pool->cond_get);
		sys_thread_mutex_unlock(pool->mutex);

		if(job.func != NULL)
		{
			(*job.func)(job.arg);
		}
	} while(1);

	sys_thread_exit(NULL);
}

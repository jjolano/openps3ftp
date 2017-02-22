#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/memory.h>

void * operator new (std::size_t sz)
{
	void *p;

	if (sz == 0)
		return NULL;

	p = malloc (sz);
	return p;
}

void operator delete (void *ptr)
{
	free(ptr);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/memory.h>

void * operator new (std::size_t sz)
{
	void *p;

	/* malloc (0) is unpredictable; avoid it. */
	if (sz == 0)
		sz = 1;

	p = malloc (sz);
	return p;
}


void operator delete (void *ptr)
{
	free(ptr);
}

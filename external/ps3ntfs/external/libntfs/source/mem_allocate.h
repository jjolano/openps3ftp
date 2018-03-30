/**
 * mem_allocate.h - Memory allocation and destruction calls.
 *
 * Copyright (c) 2009 Rhys "Shareese" Koedijk
 * Copyright (c) 2006 Michael "Chishm" Chisholm
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _MEM_ALLOCATE_H
#define _MEM_ALLOCATE_H

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <stdlib.h>

static inline void* ntfs_alloc (size_t size) {
    return malloc(size);
}

static inline void* ntfs_align (size_t size) {
    return memalign(32, size);
}

static inline void ntfs_free (void* mem) {
    free(mem);
}

static inline void* mem_alloc (size_t size) {
    return malloc(size);
}

static inline void* mem_calloc (size_t count, size_t size) {
    return calloc(count, size);
}

static inline void* mem_realloc (void *p, size_t size) {
    return realloc(p, size);
}

static inline void* mem_align (size_t a, size_t size) {
    return memalign(a, size);
}

static inline void mem_free (void* mem) {
    free(mem);
}

#endif /* _MEM_ALLOCATE_H */

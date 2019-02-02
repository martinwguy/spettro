/*	Copyright (C) 2018-2019 Martin Guy <martinwguy@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * alloc.c: Memory-allocation wrappers for malloc() and friends.
 * 
 * Having to write "if (foo == NULL) { Handle error... and exit }" is noisy,
 * and using these wrappers gives us a chance to free some memory and try
 * again instead of dying.
 *
 * free() is ok.
 */

#include "spettro.h"
#include "alloc.h"

void *Malloc(size_t size)
{
    void *mem = malloc(size);
    if (!mem) {
    	fprintf(stderr, "Cannot allocate %d bytes\n", size);
	abort();
    }
    return(mem);
}

void *Calloc(size_t nmemb, size_t size)
{
    void *mem = calloc(nmemb, size);
    if (!mem) abort();
    return(mem);
}

void *Realloc(void *ptr, size_t size)
{
    void *mem = realloc(ptr, size);
    if (!mem) abort();
    return(mem);
}

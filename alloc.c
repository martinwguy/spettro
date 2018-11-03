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
    if (!mem) abort();
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

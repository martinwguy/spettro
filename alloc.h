/* alloc.h: Declarations for alloc.c
 *
 * All callers include spettro.h, which contains the necessary #includes */

void *Malloc(size_t size);
void *Calloc(size_t nmemb, size_t size);
void *Realloc(void *ptr, size_t size);

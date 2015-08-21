#ifndef mtask_malloc_h
#define mtask_malloc_h

#include <stddef.h>

#define mtask_malloc malloc
#define mtask_calloc calloc
#define mtask_realloc realloc
#define mtask_free free

void * mtask_malloc(size_t sz);
void * mtask_calloc(size_t nmemb,size_t size);
void * mtask_realloc(void *ptr, size_t size);
void mtask_free(void *ptr);
char * mtask_strdup(const char *str);
void * mtask_lalloc(void *ud, void *ptr, size_t osize, size_t nsize);	// use for lua

#endif

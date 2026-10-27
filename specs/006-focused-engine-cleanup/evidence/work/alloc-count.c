#define _GNU_SOURCE
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
extern void *__libc_malloc(size_t);
extern void __libc_free(void *);
extern void *__libc_calloc(size_t,size_t);
extern void *__libc_realloc(void *,size_t);
static _Atomic unsigned long allocs, frees;
void *malloc(size_t n) { atomic_fetch_add(&allocs,1); return __libc_malloc(n); }
void free(void *p) { if(p) atomic_fetch_add(&frees,1); __libc_free(p); }
void *calloc(size_t n,size_t s) { atomic_fetch_add(&allocs,1); return __libc_calloc(n,s); }
void *realloc(void *p,size_t n) { atomic_fetch_add(&allocs,1); return __libc_realloc(p,n); }
__attribute__((destructor)) static void report(void) { fprintf(stderr,"ALLOC malloc_calloc_realloc=%lu frees=%lu\n",atomic_load(&allocs),atomic_load(&frees)); }

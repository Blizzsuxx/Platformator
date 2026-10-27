#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
static _Atomic unsigned long stats, links, reals;
int stat(const char *p, struct stat *s) { static int (*f)(const char *,struct stat *); if(!f) f=dlsym(RTLD_NEXT,"stat"); atomic_fetch_add(&stats,1); return f(p,s); }
int lstat(const char *p, struct stat *s) { static int (*f)(const char *,struct stat *); if(!f) f=dlsym(RTLD_NEXT,"lstat"); atomic_fetch_add(&stats,1); return f(p,s); }
ssize_t readlink(const char *p, char *b, size_t n) { static ssize_t (*f)(const char *,char *,size_t); if(!f) f=dlsym(RTLD_NEXT,"readlink"); atomic_fetch_add(&links,1); return f(p,b,n); }
char *realpath(const char *p, char *b) { static char *(*f)(const char *,char *); if(!f) f=dlsym(RTLD_NEXT,"realpath"); atomic_fetch_add(&reals,1); return f(p,b); }
__attribute__((destructor)) static void report(void) { fprintf(stderr,"INTERCEPT stat_lstat=%lu readlink=%lu realpath=%lu\n",atomic_load(&stats),atomic_load(&links),atomic_load(&reals)); }

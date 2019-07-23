/* heap.c - overrides of SDK heap handling functions
 * Copyright (c) 2016 Ivan Grokhotkov. All rights reserved.
 * This file is distributed under MIT license.
 */

#include <stdlib.h>
#include "umm_malloc/umm_malloc.h"
#include <c_types.h>
#include <sys/reent.h>


#ifdef UMM_POISON_CHECK
// These used in heap.cpp
#define __umm_malloc(s)    umm_poison_malloc(s)
#define __umm_calloc(n,s)  umm_poison_calloc(n,s)
#define __umm_realloc(p,s) umm_poison_realloc(p,s)
#define __umm_free(p)      umm_poison_free(p)

#else  // ! UMM_POISON_CHECK
#define __umm_malloc(s)    umm_malloc(s)
#define __umm_calloc(n,s)  umm_calloc(n,s)
#define __umm_realloc(p,s) umm_realloc(p,s)
#define __umm_free(p)      umm_free(p)
#endif  // UMM_POISON_CHECK


extern "C" {

// Debugging helper, last allocation which returned NULL
void *umm_last_fail_alloc_addr = NULL;
int umm_last_fail_alloc_size = 0;


void ICACHE_RAM_ATTR free (void* p)
{
    __umm_free(p);
}

void* _malloc_r(struct _reent* unused, size_t size)
{
    (void) unused;
    void *ret = malloc(size);
    if (0 != size && 0 == ret) {
        umm_last_fail_alloc_addr = __builtin_return_address(0);
        umm_last_fail_alloc_size = size;
    }
    return ret;
}

void _free_r(struct _reent* unused, void* ptr)
{
    (void) unused;
    return free(ptr);
}

void* _realloc_r(struct _reent* unused, void* ptr, size_t size)
{
    (void) unused;
    void *ret = realloc(ptr, size);
    if (0 != size && 0 == ret) {
        umm_last_fail_alloc_addr = __builtin_return_address(0);
        umm_last_fail_alloc_size = size;
    }
    return ret;
}

void* _calloc_r(struct _reent* unused, size_t count, size_t size)
{
    (void) unused;
    void *ret = calloc(count, size);
    if (0 != (count * size) && 0 == ret) {
        umm_last_fail_alloc_addr = __builtin_return_address(0);
        umm_last_fail_alloc_size = count * size;
    }
    return ret;
}

void ICACHE_RAM_ATTR vPortFree(void *ptr, const char* file, int line)
{
    (void) file;
    (void) line;
    free(ptr);
}

#ifdef DEBUG_ESP_OOM

void* ICACHE_RAM_ATTR pvPortMalloc(size_t size, const char* file, int line)
{
    return malloc_loc(size, file, line);
}

void* ICACHE_RAM_ATTR pvPortCalloc(size_t count, size_t size, const char* file, int line)
{
    return calloc_loc(count, size, file, line);
}

void* ICACHE_RAM_ATTR pvPortRealloc(void *ptr, size_t size, const char* file, int line)
{
    return realloc_loc(ptr, size, file, line);
}

void* ICACHE_RAM_ATTR pvPortZalloc(size_t size, const char* file, int line)
{
    return calloc_loc(1, size, file, line);
}

#undef malloc
#undef calloc
#undef realloc

static const char oom_fmt[]   PROGMEM STORE_ATTR = ":oom(%d)@?\n";
static const char oom_fmt_1[] PROGMEM STORE_ATTR = ":oom(%d)@";
static const char oom_fmt_2[] PROGMEM STORE_ATTR = ":%d\n";

void* ICACHE_RAM_ATTR malloc (size_t s)
{
    void* ret = __umm_malloc(s);
    if (!ret)
        os_printf(oom_fmt, (int)s);
    return ret;
}

void* ICACHE_RAM_ATTR calloc (size_t n, size_t s)
{
    void* ret = __umm_calloc(n, s);
    if (!ret)
        os_printf(oom_fmt, (int)s);
    return ret;
}

void* ICACHE_RAM_ATTR realloc (void* p, size_t s)
{
    void* ret = __umm_realloc(p, s);
    if (!ret)
        os_printf(oom_fmt, (int)s);
    return ret;
}


void print_loc (size_t s, const char* file, int line)
{
        os_printf(oom_fmt_1, (int)s);
        os_printf(file);
        os_printf(oom_fmt_2, line);
}

void* malloc_loc (size_t s, const char* file, int line)
{
    void* ret = __umm_malloc(s);
    if (!ret)
        print_loc(s, file, line);
    return ret;
}

void* calloc_loc (size_t n, size_t s, const char* file, int line)
{
    void* ret = __umm_calloc(n, s);
    if (!ret)
        print_loc(s, file, line);
    return ret;
}

void* realloc_loc (void* p, size_t s, const char* file, int line)
{
    void* ret = __umm_realloc(p, s);
    if (!ret)
        print_loc(s, file, line);
    return ret;
}

#else

void* ICACHE_RAM_ATTR malloc (size_t s)
{
    void* ret = __umm_malloc(s);
    return ret;
}

void* ICACHE_RAM_ATTR calloc (size_t n, size_t s)
{
    void* ret = __umm_calloc(n, s);
    return ret;
}

void* ICACHE_RAM_ATTR realloc (void* p, size_t s)
{
    void* ret = __umm_realloc(p, s);
    return ret;
}

void* ICACHE_RAM_ATTR pvPortMalloc(size_t size, const char* file, int line)
{
    (void) file;
    (void) line;
    return malloc(size);
}

void* ICACHE_RAM_ATTR pvPortCalloc(size_t count, size_t size, const char* file, int line)
{
    (void) file;
    (void) line;
    return calloc(count, size);
}

void* ICACHE_RAM_ATTR pvPortRealloc(void *ptr, size_t size, const char* file, int line)
{
    (void) file;
    (void) line;
    return realloc(ptr, size);
}

void* ICACHE_RAM_ATTR pvPortZalloc(size_t size, const char* file, int line)
{
    (void) file;
    (void) line;
    return calloc(1, size);
}

#endif // !defined(DEBUG_ESP_OOM)

size_t xPortGetFreeHeapSize(void)
{
    return umm_free_heap_size();
}

size_t ICACHE_RAM_ATTR xPortWantedSizeAlign(size_t size)
{
    return (size + 3) & ~((size_t) 3);
}

void system_show_malloc(void)
{
    umm_info(NULL, 1);
}

};

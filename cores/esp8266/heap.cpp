/* heap.c - overrides of SDK heap handling functions
 * Copyright (c) 2016 Ivan Grokhotkov. All rights reserved.
 * This file is distributed under MIT license.
 */

#include <stdlib.h>
#include "umm_malloc/umm_malloc.h"
#include <c_types.h>
#include <sys/reent.h>


extern "C" {

#ifdef UMM_POISON_CHECK
#define __umm_malloc(s)           umm_poison_malloc(s)
#define __umm_calloc(n,s)         umm_poison_calloc(n,s)
#define __umm_realloc_fl(p,s,f,l) umm_poison_realloc_fl(p,s,f,l)
#define __umm_free_fl(p,f,l)      umm_poison_free_fl(p,f,l)

#define POISON_CHECK__ABORT() \
    do { \
        if ( ! POISON_CHECK() ) \
            abort(); \
    } while(0)

#define POISON_CHECK__PANIC_FL(file, line) \
    do { \
        if ( ! POISON_CHECK() ) \
            __panic_func(file, line, ""); \
    } while(0)

#else  // ! UMM_POISON_CHECK
#define __umm_malloc(s)           umm_malloc(s)
#define __umm_calloc(n,s)         umm_calloc(n,s)
#define __umm_realloc(p,s)        umm_realloc(p,s)
#define __umm_free(p)             umm_free(p)

#define POISON_CHECK__ABORT() do {} while(0)
#define POISON_CHECK__PANIC_FL(file, line) do { (void)file; (void)line; } while(0)

#endif  // UMM_POISON_CHECK

// Debugging helper, last allocation which returned NULL
void *umm_last_fail_alloc_addr = NULL;
int umm_last_fail_alloc_size = 0;


#ifdef UMM_INTEGRITY_CHECK
#define INTEGRITY_CHECK__ABORT() \
    do { \
        if ( ! INTEGRITY_CHECK() ) \
            abort(); \
    } while(0)

#define INTEGRITY_CHECK__PANIC_FL(file, line) \
    do { \
        if ( ! INTEGRITY_CHECK() ) \
            __panic_func(file, line, ""); \
    } while(0)

#else  // ! UMM_INTEGRITY_CHECK
#define INTEGRITY_CHECK__ABORT() do {} while(0)
#define INTEGRITY_CHECK__PANIC_FL(file, line) do { (void)file; (void)line; } while(0)

#endif //   UMM_INTEGRITY_CHECK

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
    free(ptr);
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
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
#if defined(UMM_POISON_CHECK)
    __umm_free_fl(ptr, file, line);
#else
    __umm_free(ptr);
#endif
}

#if defined(UMM_POISON_CHECK)
#undef free
#endif

void ICACHE_RAM_ATTR free(void* p)
{
    POISON_CHECK__ABORT();
    INTEGRITY_CHECK__ABORT();
#if defined(UMM_POISON_CHECK)
    __umm_free_fl(p, NULL, 0);
#else
    __umm_free(p);
#endif
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
    POISON_CHECK__ABORT();
    INTEGRITY_CHECK__ABORT();
    void* ret = __umm_malloc(s);
    if (!ret)
        DBGLOG_FUNCTION_P(oom_fmt, (int)s);
    return ret;
}

void* ICACHE_RAM_ATTR calloc (size_t n, size_t s)
{
    POISON_CHECK__ABORT();
    INTEGRITY_CHECK__ABORT();
    void* ret = __umm_calloc(n, s);
    if (!ret)
        DBGLOG_FUNCTION_P(oom_fmt, (int)s);
    return ret;
}

void* ICACHE_RAM_ATTR realloc (void* p, size_t s)
{
    POISON_CHECK__ABORT();
    INTEGRITY_CHECK__ABORT();
#ifdef UMM_POISON_CHECK
    void* ret = __umm_realloc_fl(p, s, NULL, 0);
#else
    void* ret = __umm_realloc(p, s);
#endif
    if (!ret)
        DBGLOG_FUNCTION_P(oom_fmt, (int)s);
    return ret;
}

void ICACHE_RAM_ATTR print_loc (size_t s, const char* file, int line)
{
        DBGLOG_FUNCTION_P(oom_fmt_1, (int)s);
        DBGLOG_FUNCTION_P(file);
        DBGLOG_FUNCTION_P(oom_fmt_2, line);
}

void* ICACHE_RAM_ATTR malloc_loc (size_t s, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
    void* ret = __umm_malloc(s);
    if (!ret)
        print_loc(s, file, line);
    return ret;
}

void* ICACHE_RAM_ATTR calloc_loc (size_t n, size_t s, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
    void* ret = __umm_calloc(n, s);
    if (!ret)
        print_loc(s, file, line);
    return ret;
}

void* ICACHE_RAM_ATTR realloc_loc (void* p, size_t s, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
#ifdef UMM_POISON_CHECK
    void* ret = __umm_realloc_fl(p, s, file, line);
#else
    void* ret = __umm_realloc(p, s);
#endif
    if (!ret)
        print_loc(s, file, line);
    return ret;
}

#else

#ifdef UMM_POISON_CHECK

#undef realloc

void* ICACHE_RAM_ATTR realloc_loc (void* p, size_t s, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
    return __umm_realloc_fl(p, s, file, line);
}

void ICACHE_RAM_ATTR free_loc (void* p, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
    __umm_free_fl(p, file, line);
}

void* ICACHE_RAM_ATTR realloc (void* p, size_t s)
{
    POISON_CHECK__ABORT();
    INTEGRITY_CHECK__ABORT();
    return __umm_realloc_fl(p, s, NULL, 0);
}

#else  // ! UMM_POISON_CHECK

void* ICACHE_RAM_ATTR realloc (void* p, size_t s)
{
    INTEGRITY_CHECK__ABORT();
    return __umm_realloc(p, s);
}
#endif  // UMM_POISON_CHECK

void* ICACHE_RAM_ATTR malloc (size_t s)
{
    POISON_CHECK__ABORT();
    INTEGRITY_CHECK__ABORT();
    return __umm_malloc(s);
}

void* ICACHE_RAM_ATTR calloc (size_t n, size_t s)
{
    POISON_CHECK__ABORT();
    INTEGRITY_CHECK__ABORT();
    return __umm_calloc(n, s);
}

void* ICACHE_RAM_ATTR pvPortMalloc(size_t size, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
    return __umm_malloc(size);
}

void* ICACHE_RAM_ATTR pvPortCalloc(size_t count, size_t size, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
    return __umm_calloc(count, size);
}

void* ICACHE_RAM_ATTR pvPortRealloc(void *ptr, size_t size, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
#ifdef UMM_POISON_CHECK
    return __umm_realloc_fl(ptr, size, file, line);
#else
    return __umm_realloc(ptr, size);
#endif
}

void* ICACHE_RAM_ATTR pvPortZalloc(size_t size, const char* file, int line)
{
    POISON_CHECK__PANIC_FL(file, line);
    INTEGRITY_CHECK__PANIC_FL(file, line);
    return __umm_calloc(1, size);
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

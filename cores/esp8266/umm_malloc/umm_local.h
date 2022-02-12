#ifndef _UMM_LOCAL_H
#define _UMM_LOCAL_H

/*
 * A home for local items exclusive to umm_malloc.c and not to be shared in
 * umm_malloc_cfg.h. And, not for upstream version.
 * Also used to redefine defines made in upstream files we donet want to edit.
 *
 */

#undef memcpy
#undef memmove
#undef memset
#define memcpy ets_memcpy
#define memmove ets_memmove
#define memset ets_memset


/*
 * Saturated unsigned add and unsigned multiply
 */
size_t umm_umul_sat(const size_t a, const size_t b);  // share with heap.cpp
#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
static size_t umm_uadd_sat(const size_t a, const size_t b);
#endif


#if defined(DEBUG_ESP_OOM) || defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_INTEGRITY_CHECK) || (UMM_POINTER_CHECK >= 2)
#define UMM_POINTER_1_LOG_CALLER() (void)0

#else
#if (UMM_POINTER_CHECK == 1) // +16 bytes
#define UMM_POINTER_1_LOG_CALLER() umm_ptr_check_results.caller = __builtin_return_address(0)
#else
#define UMM_POINTER_1_LOG_CALLER() (void)0
#endif
#define umm_malloc(s)    malloc(s)
#define umm_calloc(n,s)  calloc(n,s)
#define umm_realloc(p,s) realloc(p,s)
#define umm_free(p)      free(p)
#endif


#if defined(UMM_POISON_CHECK_LITE)
static bool check_poison_neighbors(const umm_heap_context_t * const _context, const uint16_t cur);
#endif


#if (UMM_STATS > 0)
void ICACHE_FLASH_ATTR umm_print_stats(void);
#endif


/*
 * This redefines DBGLOG_FORCE defined in dbglog/dbglog.h
 * Just for printing from umm_info() which is assumed to always be called from
 * non-ISR. Thus SPI bus is available to handle cache-miss and reading a flash
 * string while INTLEVEL is non-zero.
 */
#undef DBGLOG_FORCE
#define DBGLOG_FORCE(force, format, ...) {if (force) {UMM_INFO_PRINTF(format,##__VA_ARGS__);}}

int ICACHE_FLASH_ATTR umm_info_safe_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define UMM_INFO_PRINTF(fmt, ...) umm_info_safe_printf_P(PSTR(fmt),##__VA_ARGS__)


////////////////////////////////////////////////////////////////////////////////
//

#if (UMM_POINTER_CHECK >= 1) || defined(UMM_ENABLE_CHECK_WRAPPERS)
enum PTR_CHECK_RC {
    none = 0,
    good,
    corrupt,            // Outside of defined heap space
  # if (UMM_POINTER_CHECK == 3)
    free_space,         // In unallocated heap space
    inside,             // Inside an active allocation
    #else
    free_mark,          // Allocation has free bit set
    #endif
    bad,                // PTR does not appear valid
    accuracy,           // PTR value does not match UMM_DATA address is a few bytes off
    poison,             // Poison check failed
    integrity
};

struct UMM_PTR_CHECK_RESULTS {
    uintptr_t     sketch_ptr; // As would be seen by the sketch author
    const void*   caller;
    PTR_CHECK_RC  rc;
    const void*   notify;     // When set, indicates to abort with message
    #if defined(UMM_ENABLE_CHECK_WRAPPERS)
    const void*   file;
    int           line;
    #endif
    #if (UMM_POINTER_CHECK == 3)
    uintptr_t     near_ptr;   // filled in by umm_find_alloc_core
    uintptr_t     near_next;  // filled in by umm_find_alloc_core
    #endif
};

extern struct UMM_PTR_CHECK_RESULTS umm_ptr_check_results;

#endif

#if defined(UMM_ENABLE_CHECK_WRAPPERS)
void umm_check_wrapper(
    const void* const caller,
    const void* const sketch_ptr,
    const char* const file,
    const int line,
    bool (*fn_check)(const umm_heap_context_t* const, struct UMM_PTR_CHECK_RESULTS *, const void* const));
#endif


#if (UMM_POINTER_CHECK == 1)
static umm_heap_context_t* umm_data_ptr_integrity_check(const void* const ptr);
#endif

#if defined(UMM_POISON_CHECK)
static bool umm_poison_check_core(const umm_heap_context_t* const _context);
#endif

#if defined(UMM_INTEGRITY_CHECK)
static bool umm_integrity_check_core(const umm_heap_context_t* const _context);
#endif

#endif

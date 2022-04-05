#ifndef _UMM_MALLOC_CFGPORT_H
#define _UMM_MALLOC_CFGPORT_H

#ifndef _UMM_MALLOC_CFG_H
#error "This include file must be used with umm_malloc_cfg.h"
#endif

/*
 * Arduino ESP8266 core umm_malloc port config
 */
#include <pgmspace.h>
#include <mmu_iram.h>
#include "../debug.h"
#include "../esp8266_undocumented.h"

#include <core_esp8266_features.h>
#include <stdlib.h>
#include <osapi.h>

#include "c_types.h"

/*
 * While UMM_BEST_FIT is the better option for reducing heap fragmentation.
 * UMM_BEST_FIT is left unset. This leaves the UMM_BEST_FIT or UMM_FIRST_FIT
 * algorithm selection to default to UMM_BEST_FIT. This accommodates algorithm
 * selection at build time.
 * See umm_malloc_cfg.h for more information.
 */

/*
 * To support API call, system_show_malloc(), -DUMM_INFO is required.
 *
 * For the ESP8266 we need an ISR safe function to call for implementing
 * xPortGetFreeHeapSize(). We can get this with one of these options:
 *   1) -DUMM_STATS or -DUMM_STATS_FULL
 *   2) -DUMM_INLINE_METRICS (and implicitly includes -DUMM_INFO)
 *
 * If frequent calls are made to ESP.getHeapFragmentation(),
 * -DUMM_INLINE_METRICS would reduce long periods of interrupts disabled caused
 * by frequent calls to `umm_info()`. Instead, the computations get distributed
 * across each malloc, realloc, and free. This appears to require an additional
 * 116 bytes of IRAM vs using `UMM_STATS` with `UMM_INFO`.
 *
 * When both UMM_STATS and UMM_INLINE_METRICS are defined, macros and structures
 * have been optimized to reduce duplications.
 *
 */
 // #define UMM_INLINE_METRICS

/*
 * If undefined we default UMM_STATS to 1. This is the best option for memory
 * constrained builds. `UMM_STATS 2` uses a little more IRAM; however, it can
 * provide precise heap low watermarks for debuging.
 */
 // TODO comment these three out before push
 // #define UMM_STATS 2
 #ifndef UMM_STATS
 #define UMM_STATS 1
 #endif
// #define UMM_STATS 10 // UMM_STATS_FULL
// #define UMM_CRITICAL_METRICS 1

//////////////////////////////////////////////////////////////////////////////
/*
 * -DUMM_POINTER_CHECK
 *
 * Debug build option to perform quick sanity checks on supplied pointer at free
 * or realloc. On fail an error is printed via postmortem.
 *
 * Cost about ??
 *
 * Values
 *
 *   0   - Feature is off. No pointer checking.
 *         aka the "Trust me, I know what I am doing" option.
 *
 *   1   - Lite sanity testing. If defined without value, the value is set to 1.
 *         Cost: IROM +28, IRAM +112 bytes
 *
 *   2   - More detailed testing of the pointer. Enables debug Heap wrappers in
 *         heap.cpp, which may log file and line number of the caller.
 *
 *   3   - Performs a more time consuming sequential search through the
 *         umm_block list looking for a match. Enables debug Heap wrappers in
 *         heap.cpp, which may log file and line number of the caller.
 *
 *       - When undefined, auto select mode. Feature stays off unless one of
 *         these debug options is present: DEBUG_ESP_OOM or
 *         UMM_POISON_CHECK_LITE
 *         Define UMM_POINTER_CHECK 0 to block this behavior.
 *
 * Side notes, UMM_INTEGRITY_CHECK and UMM_POISON_CHECK are coded for; however,
 * the Arduino IDE has no selection to build with them. Both are CPU intensive
 * and can adversly effect the WiFi operation. We have option
 * UMM_POISON_CHECK_LITE to replace UMM_POISON_CHECK. This is include in
 * the debug build when you select the Debug Port.
 */
// #define UMM_POINTER_CHECK 1

/*
 * -DUMM_INIT_USE_IRAM
 *
 * Historically, the umm_init() call path has been in IRAM. The umm_init() call
 * path is now in ICACHE (flash). Use the build option UMM_INIT_USE_IRAM to
 * restore the legacy behavor.
 *
 * If you have your own app_entry_redefinable() function, see
 * app_entry_redefinable() in core_esp8266_app_entry_noextra4k.cpp for an
 * example of how to toggle between ICACHE and IRAM in your build.
 *
 * The default is to use ICACHE.
 */
// #define UMM_INIT_USE_IRAM 1


/*
 * UMM_INFO_NO_PRINT Reduces flash image size by about 1.1KB.
 * 1) removes printing support from umm_info() in the build
 *
 */
// #define UMM_INFO_NO_PRINT 1

/*
 * -DUMM_VARIABLE_BLOCK_SIZE
 *
 * UMM_VARIABLE_BLOCK_SIZE calculates the optimum UMM_BLOCK_BODY_SIZE needed for
 * umm_malloc to manage the entire size of the memory specified. A unique
 * UMM_BLOCK_BODY_SIZE value is calculated for each heap.
 *
 * Uses 76 bytes more IRAM and 64 bytes more IROM with UMM_INIT_USE_IRAM = 0
 * Cost to make selectable +16 bytes IROM
 */
// #define UMM_VARIABLE_BLOCK_SIZE

/*
 * Enables a function to dump the heap contents and also returns the total
 * heap size that is unallocated - note this is not the same as the largest
 * unallocated block on the heap!
 */
#ifndef UMM_INFO
#define UMM_INFO
#endif

#define DBGLOG_ENABLE
/*
 * Minimum printing only DBGLOG_FORCE(...) will prints. It is used by umm_info().
 * No need to change this value. It is redefined when needed for Debug enabled builds.
 */
#define DBGLOG_LEVEL 0

///////////////////////////////////////////////////////////////////////////////
// No configuration options below this line
///////////////////////////////////////////////////////////////////////////////

typedef struct umm_heap_config umm_heap_context_t;

/*
 * Start addresses and the size of the heap
 */
extern char _heap_start[];
#define UMM_HEAP_END_ADDR          0x3FFFC000UL
#define UMM_MALLOC_CFG_HEAP_ADDR   ((uint32_t)&_heap_start[0])
#define UMM_MALLOC_CFG_HEAP_SIZE   ((size_t)(UMM_HEAP_END_ADDR - UMM_MALLOC_CFG_HEAP_ADDR))

/*
 * Define active Heaps
 */
#if defined(MMU_IRAM_HEAP)
#define UMM_HEAP_IRAM
#else
#undef UMM_HEAP_IRAM
#endif

#if defined(MMU_EXTERNAL_HEAP)
#define UMM_HEAP_EXTERNAL
#else
#undef UMM_HEAP_EXTERNAL
#endif

/*
 * Assign IDs to active Heaps and tally. DRAM is always active.
 */
#define UMM_HEAP_DRAM 0
#define UMM_HEAP_DRAM_DEFINED 1

#ifdef UMM_HEAP_IRAM
#undef UMM_HEAP_IRAM
#define UMM_HEAP_IRAM_DEFINED 1
#define UMM_HEAP_IRAM UMM_HEAP_DRAM_DEFINED
#else
#define UMM_HEAP_IRAM_DEFINED 0
#endif

#ifdef UMM_HEAP_EXTERNAL
#undef UMM_HEAP_EXTERNAL
#define UMM_HEAP_EXTERNAL_DEFINED 1
#define UMM_HEAP_EXTERNAL (UMM_HEAP_DRAM_DEFINED + UMM_HEAP_IRAM_DEFINED)
#else
#define UMM_HEAP_EXTERNAL_DEFINED 0
#endif

#define UMM_NUM_HEAPS (UMM_HEAP_DRAM_DEFINED + UMM_HEAP_IRAM_DEFINED + UMM_HEAP_EXTERNAL_DEFINED)

#if (UMM_NUM_HEAPS == 1)
#else
#define UMM_HEAP_STACK_DEPTH 32
#endif


/* -------------------------------------------------------------------------- */


extern ICACHE_FLASH_ATTR size_t umm_num_blocks(void);
extern ICACHE_FLASH_ATTR size_t umm_heap_id(void);

#ifdef UMM_INFO
// umm_max_block_size changed to umm_max_free_block_size in upstream.
extern size_t umm_max_block_size(void) __attribute__((deprecated("Replace with 'umm_max_free_block_size()'.")));
extern ICACHE_FLASH_ATTR void umm_get_heap_stats(uint32_t *hfree, uint32_t *hmax, uint8_t *hfrag);
#else
#define umm_max_block_size() (0)
#endif


// We move our extra stuff for OOM count into macro UMM_INFO_EXTRA
#ifdef UMM_INFO
#ifdef UMM_INLINE_METRICS
#define UMM_INFO_EXTRA size_t oom_count
#define UMM_OOM_COUNT info.oom_count
#define UMM_FREE_BLOCKS info.freeBlocks
#endif
#endif

/*
 * -D UMM_STATS :
 * -D UMM_STATS 2:
 * -D UMM_STATS_FULL
 *
 * This option provides a lightweight alternative to using `umm_info` just for
 * getting `umm_free_heap_size`.  With this option, a "free blocks" value is
 * updated on each call to malloc/free/realloc. This option does not offer all
 * the information that `umm_info` would have generated.
 *
 * This option is good for cases where the free heap is checked frequently. An
 * example is when an app closely monitors free heap to detect memory leaks. In
 * this case a single-core CPUs interrupt processing would have suffered the
 * most.
 *
 * UMM_STATS 2, includes code to monitor for and save a heap low water mark.
 * Cost 68 bytes of IRAM in a multi-heap build.and 74 bytes if IROM.
 * TODO why IROM? Concern something may be in IROM that should not!
 *
 * UMM_STATS_FULL provides additional heap statistics. It can be used to gain
 * additional insight into heap usage. This option would add an additional 132
 * bytes of IRAM.
 *
 * Status: TODO: Needs to be proposed for upstream.
 */
/*
#define UMM_STATS 1
#define UMM_STATS 2
#define UMM_STATS_FULL
 */

#if !defined(UMM_STATS) && !defined(UMM_STATS_FULL) && !defined(UMM_INLINE_METRICS)
#define UMM_STATS 1
#endif

#if defined(UMM_STATS) && defined(UMM_STATS_FULL)
#undef UMM_STATS
#endif

#if defined(UMM_STATS_FULL)
#undef UMM_STATS_FULL
#define UMM_STATS 10
#endif

/*
 * Ensure UMM_STATS has a numeric a value.
 */
#if defined(UMM_STATS)

#if ((1-UMM_STATS-1) == 2)
// Assume 1 for define w/o value
#undef UMM_STATS
#define UMM_STATS 1
#endif

#else
#define UMM_STATS 0
#endif


#if (UMM_STATS > 0)

typedef struct UMM_STATISTICS_t {
    #ifndef UMM_INLINE_METRICS
// If we are doing UMM_INLINE_METRICS, we can move oom_count and free_blocks to
// umm_info's structure and save a little DRAM and IRAM.
// Otherwise it is defined here.
    size_t free_blocks;
    size_t oom_count;
  #define UMM_OOM_COUNT stats.oom_count
  #define UMM_FREE_BLOCKS stats.free_blocks
    #endif
    #if (UMM_STATS > 1)
    size_t free_blocks_min;
    #endif
    #if (UMM_STATS >= 10)   // UMM_STATS_FULL
    size_t free_blocks_isr_min;
    size_t alloc_max_size;
    size_t last_alloc_size;
    size_t id_malloc_count;
    size_t id_malloc_zero_count;
    size_t id_realloc_count;
    size_t id_realloc_zero_count;
    size_t id_free_count;
    size_t id_free_null_count;
    #endif
}
UMM_STATISTICS;

#ifdef UMM_INLINE_METRICS
#define STATS__FREE_BLOCKS_UPDATE(s) (void)(s)
#else
#define STATS__FREE_BLOCKS_UPDATE(s) _context->stats.free_blocks += (s)
#endif

#define STATS__OOM_UPDATE() _context->UMM_OOM_COUNT += 1

// extern size_t umm_free_heap_size_lw(void);
extern size_t umm_get_oom_count(void);

#else  // not UMM_STATS or UMM_STATS_FULL
#define STATS__FREE_BLOCKS_UPDATE(s) (void)(s)
#define STATS__OOM_UPDATE()          (void)0
#endif

#if (UMM_STATS > 0) || defined(UMM_INFO)
size_t ICACHE_FLASH_ATTR umm_block_size(void);
#endif

#if (UMM_STATS > 1)
/*
 * Monitory heap low water mark
 */
#define STATS__FREE_BLOCKS_MIN() \
    do { \
        if (_context->UMM_FREE_BLOCKS < _context->stats.free_blocks_min) { \
            _context->stats.free_blocks_min = _context->UMM_FREE_BLOCKS; \
        } \
    } while (false)

    size_t umm_free_heap_size_min(void);
    size_t umm_free_heap_size_lw_min(void) __attribute__((deprecated("Replace with 'umm_free_heap_size_min()'.")));;
    size_t umm_free_heap_size_min_reset(void);
#else
#define STATS__FREE_BLOCKS_MIN()          (void)0
#endif

#if (UMM_STATS >= 10) // UMM_STATS_FULL
#define STATS__FREE_BLOCKS_ISR_MIN() \
    do { \
        if (_context->UMM_FREE_BLOCKS < _context->stats.free_blocks_isr_min) { \
            _context->stats.free_blocks_isr_min = _context->UMM_FREE_BLOCKS; \
        } \
    } while (false)

#define STATS__ALLOC_REQUEST(tag, s)  \
    do { \
        _context->stats.tag##_count += 1; \
        _context->stats.last_alloc_size = s; \
        if (_context->stats.alloc_max_size < s) { \
            _context->stats.alloc_max_size = s; \
        } \
    } while (false)

#define STATS__ZERO_ALLOC_REQUEST(tag, s)  \
    do { \
        _context->stats.tag##_zero_count += 1; \
    } while (false)

#define STATS__NULL_FREE_REQUEST(tag)  \
    do { \
        umm_heap_context_t *_context = umm_get_current_heap(); \
        _context->stats.tag##_null_count += 1; \
    } while (false)

#define STATS__FREE_REQUEST(tag)  \
    do { \
        _context->stats.tag##_count += 1; \
    } while (false)


size_t umm_free_heap_size_isr_min(void);
size_t umm_get_max_alloc_size(void);
size_t umm_get_last_alloc_size(void);
size_t umm_get_malloc_count(void);
size_t umm_get_malloc_zero_count(void);
size_t umm_get_realloc_count(void);
size_t umm_get_realloc_zero_count(void);
size_t umm_get_free_count(void);
size_t umm_get_free_null_count(void);

#else // Not UMM_STATS_FULL
#define STATS__FREE_BLOCKS_ISR_MIN()      (void)0
#define STATS__ALLOC_REQUEST(tag, s)      (void)(s)
#define STATS__ZERO_ALLOC_REQUEST(tag, s) (void)(s)
#define STATS__NULL_FREE_REQUEST(tag)     (void)0
#define STATS__FREE_REQUEST(tag)          (void)0
#endif

/*
  Per Devyte, the core currently doesn't support masking a specific interrupt
  level. That doesn't mean it can't be implemented, only that at this time
  locking is implemented as all or nothing.
  https://github.com/esp8266/Arduino/issues/6246#issuecomment-508612609

  So for now we default to all, 15.
 */
#ifndef DEFAULT_CRITICAL_SECTION_INTLEVEL
#define DEFAULT_CRITICAL_SECTION_INTLEVEL 15
#endif

/*
 * -D UMM_CRITICAL_METRICS
 *
 * Build option to collect timing usage data on critical section usage in
 * functions: info, malloc, realloc. Collects MIN, MAX, and number of time IRQs
 * were disabled at request time. Note, for realloc MAX disabled time will
 * include the time spent in calling malloc and/or free. Examine code for
 * specifics on what info is available and how to access.
 *
 * Status: TODO: Needs to be proposed for upstream. Also should include updates
 * to UMM_POISON_CHECK and UMM_INTEGRITY_CHECK to include a critical section.
 */
/*
#define UMM_CRITICAL_METRICS
 */

#if defined(UMM_CRITICAL_METRICS)
// This option adds support for gathering time locked data

typedef struct UMM_TIME_STAT_t {
    uint32_t min;
    uint32_t max;
    uint32_t start;
    uint32_t intlevel;
}
UMM_TIME_STAT;

typedef struct UMM_TIME_STATS_t UMM_TIME_STATS;

extern UMM_TIME_STATS time_stats;

bool get_umm_get_perf_data(UMM_TIME_STATS *p, size_t size);

static inline void _critical_entry(UMM_TIME_STAT *p, uint32_t *saved_ps) {
    *saved_ps = xt_rsil(DEFAULT_CRITICAL_SECTION_INTLEVEL);
    if (0U != (*saved_ps & 0x0FU)) {
        p->intlevel += 1U;
    }

    p->start = esp_get_cycle_count();
}

static inline void _critical_exit(UMM_TIME_STAT *p, uint32_t *saved_ps) {
    uint32_t elapse = esp_get_cycle_count() - p->start;
    if (elapse < p->min) {
        p->min = elapse;
    }

    if (elapse > p->max) {
        p->max = elapse;
    }

    xt_wsr_ps(*saved_ps);
}
#endif
//////////////////////////////////////////////////////////////////////////////////////


/*
 * A couple of macros to make it easier to protect the memory allocator
 * in a multitasking system. You should set these macros up to use whatever
 * your system uses for this purpose. You can disable interrupts entirely, or
 * just disable task switching - it's up to you
 *
 * NOTE WELL that these macros MUST be allowed to nest, because umm_free() is
 * called from within umm_malloc()
 */

// Note, UMM_MAX_CRITICAL_DEPTH_CHECK is not implimented and by way of these
// defines it becomes a NO-OP.
#if defined(UMM_CRITICAL_METRICS)
    #define UMM_CRITICAL_DECL(tag) uint32_t _saved_ps_##tag
    #define UMM_CRITICAL_ENTRY(tag)_critical_entry(&time_stats.tag, &_saved_ps_##tag)
    #define UMM_CRITICAL_EXIT(tag) _critical_exit(&time_stats.tag, &_saved_ps_##tag)
    #define UMM_CRITICAL_WITHINISR(tag) (0 != (_saved_ps_##tag & 0x0F))

#else  // ! UMM_CRITICAL_METRICS
// This method preserves the intlevel on entry and restores the
// original intlevel at exit.
    #define UMM_CRITICAL_DECL(tag) uint32_t _saved_ps_##tag
    #define UMM_CRITICAL_ENTRY(tag) _saved_ps_##tag = xt_rsil(DEFAULT_CRITICAL_SECTION_INTLEVEL)
    #define UMM_CRITICAL_EXIT(tag) xt_wsr_ps(_saved_ps_##tag)
    #define UMM_CRITICAL_WITHINISR(tag) (0 != (_saved_ps_##tag & 0x0F))
#endif

/*
  * -D UMM_LIGHTWEIGHT_CPU
  *
  * The use of this macro is hardware/application specific.
  *
  * With some CPUs, the only available method for locking are the instructions
  * for interrupts disable/enable. These macros are meant for lightweight single
  * CPU systems that are sensitive to interrupts being turned off for too long. A
  * typically UMM_CRITICAL_ENTRY would save current IRQ state then disable IRQs.
  * Then UMM_CRITICAL_EXIT would restore previous IRQ state. This option adds
  * additional critical entry/exit points by the method of defining the macros
  * UMM_CRITICAL_SUSPEND and  UMM_CRITICAL_RESUME to the values of
  * UMM_CRITICAL_EXIT and UMM_CRITICAL_ENTRY.  These additional exit/entries
  * allow time to service interrupts during the reentrant sections of the code.
  *
  * Performance may be impacked if used with multicore CPUs. The higher frquency
  * of locking and unlocking may be an issue with locking methods that have a
  * high overhead.
  *
  * Status: TODO: Needs to be proposed for upstream.
  */
/*
 */
#define UMM_LIGHTWEIGHT_CPU

#ifdef UMM_LIGHTWEIGHT_CPU
#define UMM_CRITICAL_SUSPEND(tag) UMM_CRITICAL_EXIT(tag)
#define UMM_CRITICAL_RESUME(tag) UMM_CRITICAL_ENTRY(tag)
#else
#define UMM_CRITICAL_SUSPEND(tag) do {} while (0)
#define UMM_CRITICAL_RESUME(tag) do {} while (0)
#endif

/*
 * -D UMM_REALLOC_MINIMIZE_COPY   or
 * -D UMM_REALLOC_DEFRAG
 *
 * Pick one of these two stratagies. UMM_REALLOC_MINIMIZE_COPY grows upward or
 * shrinks an allocation, avoiding copy when possible. UMM_REALLOC_DEFRAG gives
 * priority with growing the revised allocation toward an adjacent hole in the
 * direction of the beginning of the heap when possible.
 *
 * Status: TODO: These are new options introduced to optionally restore the
 * previous defrag property of realloc. The issue has been raised in the upstream
 * repo. No response at this time. Based on response, may propose for upstream.
 */
/*
#define UMM_REALLOC_MINIMIZE_COPY
*/
#define UMM_REALLOC_DEFRAG

/*
 * -D UMM_INTEGRITY_CHECK :
 *
 * Enables heap integrity check before any heap operation. It affects
 * performance, but does NOT consume extra memory.
 *
 * If integrity violation is detected, the message is printed and user-provided
 * callback is called: `UMM_HEAP_CORRUPTION_CB()`
 *
 * Note that not all buffer overruns are detected: each buffer is aligned by
 * 4 bytes, so there might be some trailing "extra" bytes which are not checked
 * for corruption.
 */

/*
 * Not normally enabled. Full intergity check may exceed 10us.
 */
/*
#define UMM_INTEGRITY_CHECK
 */
#if defined(UMM_INTEGRITY_CHECK) || defined(DEBUG_ESP_PORT) || defined(DEBUG_ESP_CORE)
extern bool umm_integrity_check_ctx(umm_heap_context_t *_context);
extern bool umm_integrity_check(void);
#endif

/////////////////////////////////////////////////

/*
 * -D UMM_POISON_CHECK :
 * -D UMM_POISON_CHECK_LITE
 *
 * Enables heap poisoning: add predefined value (poison) before and after each
 * allocation, and check before each heap operation that no poison is
 * corrupted.
 *
 * Other than the poison itself, we need to store exact user-requested length
 * for each buffer, so that overrun by just 1 byte will be always noticed.
 *
 * Customizations:
 *
 *    UMM_POISON_SIZE_BEFORE:
 *      Number of poison bytes before each block, e.g. 4
 *    UMM_POISON_SIZE_AFTER:
 *      Number of poison bytes after each block e.g. 4
 *    UMM_POISONED_BLOCK_LEN_TYPE
 *      Type of the exact buffer length, e.g. `uint16_t`
 *
 * NOTE: each allocated buffer is aligned by 4 bytes. But when poisoning is
 * enabled, actual pointer returned to user is shifted by
 * `(sizeof(UMM_POISONED_BLOCK_LEN_TYPE) + UMM_POISON_SIZE_BEFORE)`.
 * It's your responsibility to make resulting pointers aligned appropriately.
 *
 * If poison corruption is detected, the message is printed and user-provided
 * callback is called: `UMM_HEAP_CORRUPTION_CB()`
 *
 * UMM_POISON_CHECK - does a global heap check on all active allocation at
 * every alloc API call. May exceed 10us due to critical section with IRQs
 * disabled.
 *
 * UMM_POISON_CHECK_LITE - checks the allocation presented at realloc()
 * and free(). Expands the poison check on the current allocation to
 * include its nearest allocated neighbors in the heap.
 * umm_malloc() will also checks the neighbors of the selected allocation
 * before use.
 *
 * Status: TODO?: UMM_POISON_CHECK_LITE is a new option. We could propose for
 * upstream; however, the upstream version has much of the framework for calling
 * poison check on each alloc call refactored out. Not sure how this will be
 * received.
 */

/*
 * Compatibility for deprecated UMM_POISON
 */
#if defined(UMM_POISON) && !defined(UMM_POISON_CHECK) && !defined(UMM_TAG_POISON_CHECK)
#define UMM_POISON_CHECK_LITE
#endif

#if defined(DEBUG_ESP_PORT) || defined(DEBUG_ESP_CORE)
#if !defined(UMM_POISON_CHECK) && !defined(UMM_POISON_CHECK_LITE) && !defined(UMM_TAG_POISON_CHECK)
/*
#define UMM_POISON_CHECK
 */
 #define UMM_POISON_CHECK_LITE
#endif
#endif


#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK)
  #define UMM_POISON_SIZE_BEFORE (4)
  #define UMM_POISON_SIZE_AFTER (4)
  #define UMM_POISONED_BLOCK_LEN_TYPE uint32_t

extern void *umm_poison_malloc(size_t size);
extern void *umm_poison_calloc(size_t num, size_t size);
extern void *umm_poison_realloc(void *ptr, size_t size);
extern void  umm_poison_free(void *ptr);
extern bool  umm_poison_check(void);
extern bool  umm_poison_check_ctx(umm_heap_context_t *_context);

// Local Additions to better report location in code of the caller.
extern void *umm_poison_malloc_flc(size_t size, const char *file, int line, const void* const caller);
extern void *umm_poison_calloc_flc(size_t num, size_t size, const char *file, int line, const void* const caller);
void *umm_poison_realloc_flc(void *ptr, size_t size, const char *file, int line, const void* const caller);
void umm_poison_free_flc(void *ptr, const char *file, int line, const void* const caller);
   #if defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK)
/*
    * We can safely do individual poison checks at free and realloc and stay
    * under 10us or close.
    */
   #define POISON_CHECK() 1
   #define POISON_CHECK_NEIGHBORS(c) \
    do { \
        if (!check_poison_neighbors(_context, c)) \
        panic(); \
    } while (false)
   #else
/* Not normally enabled. A full heap poison check may exceed 10us. */
   #define POISON_CHECK() umm_poison_check()
   #define POISON_CHECK_NEIGHBORS(c) do {} while (false)
   #endif
#else
#define POISON_CHECK() 1
#define POISON_CHECK_NEIGHBORS(c) do {} while (false)
#endif


#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK)
/*
 * Overhead adjustments needed for free_blocks to express the number of bytes
 * that can actually be allocated.
 */
// umm_block_size() / 2 +
#define UMM_OVERHEAD_ADJUST ( \
    4 + \
    UMM_POISON_SIZE_BEFORE + \
    UMM_POISON_SIZE_AFTER + \
    sizeof(UMM_POISONED_BLOCK_LEN_TYPE))

#else
#define UMM_OVERHEAD_ADJUST  (4) // (umm_block_size() / 2)
#endif


/////////////////////////////////////////////////
//C TODO  audit prints

#undef DBGLOG_FUNCTION
#undef DBGLOG_FUNCTION_P

#if defined(DEBUG_ESP_PORT) || defined(DEBUG_ESP_OOM) || \
    defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || \
    defined(UMM_INTEGRITY_CHECK) || defined(UMM_TAG_POISON_CHECK)
#define DBGLOG_FUNCTION(fmt, ...) ets_uart_printf(fmt,##__VA_ARGS__)
#else
#define DBGLOG_FUNCTION(fmt, ...)   do { (void)fmt; } while (false)
#endif

/////////////////////////////////////////////////

//C TODO Need to follow this DBGLOG level stuff again to see if it is right
// I keep getting confused on these levels and how they work.

#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK) || defined(UMM_INTEGRITY_CHECK)
#if !defined(DBGLOG_LEVEL) || DBGLOG_LEVEL < 4
// To ensure we get the debug log prints for these cases, DBGLOG_LEVEL must be
// 4 or greater.
// All debug prints for UMM_POISON_CHECK... are level 3, eg. DBGLOG_ERROR
// All debug prints for UMM_INTEGRITY_CHECK are level 4, eg. DBGLOG_CRITICAL
#undef DBGLOG_LEVEL
#define DBGLOG_LEVEL 4
#endif
#endif


/*
 * Ensure UMM_POINTER_CHECK has a numeric a value.
 */
#if defined(UMM_POINTER_CHECK)

#if ((1-UMM_POINTER_CHECK-1) == 2)
// Assume 1 for define w/o value
#undef UMM_POINTER_CHECK
#define UMM_POINTER_CHECK 1
#endif

#else
#if defined(DEBUG_ESP_OOM) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK)
// If we are going DEBUG_ESP_OOM or UMM_POISON_CHECK_LITE it is worth doing UMM_POINTER_CHECK
#define UMM_POINTER_CHECK 2
#else
#define UMM_POINTER_CHECK 0
#endif
#endif


#if (UMM_POINTER_CHECK == 1) && (defined(DEBUG_ESP_OOM) || defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK) || defined(UMM_INTEGRITY_CHECK))
// Must raise level to capture the correct caller address because of the debug wrappers in heap.cpp
#undef UMM_POINTER_CHECK
#define UMM_POINTER_CHECK 2
#endif

#if (UMM_POINTER_CHECK >= 2) || defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK) || defined(UMM_INTEGRITY_CHECK)
#define UMM_ENABLE_CHECK_WRAPPERS 1
#endif

#if (UMM_POINTER_CHECK >= 2)
extern void umm_pointer_check_wrap(const void* const ptr, const char* const file, const int line, const void* const caller);
#endif



#if defined(BUILD_UMM_MALLOC_C)
#ifndef VALUE
#define VALUE(x) __STRINGIFY(x)
#define VAR_NAME_VALUE(var) #var "="  VALUE(var)
#endif
#pragma message("UMM_POINTER_CHECK == - " VALUE(UMM_POINTER_CHECK) " -")
#endif



#if defined(UMM_CRITICAL_METRICS)
struct UMM_TIME_STATS_t {
    UMM_TIME_STAT id_malloc;
    UMM_TIME_STAT id_realloc;
    UMM_TIME_STAT id_free;
    #ifdef UMM_INFO
    UMM_TIME_STAT id_info;
    #endif
    #if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK)
    UMM_TIME_STAT id_poison;
    #endif
    #ifdef UMM_INTEGRITY_CHECK
    UMM_TIME_STAT id_integrity;
    #endif
    #ifdef UMM_ENABLE_CHECK_WRAPPERS
    UMM_TIME_STAT id_check;
    #endif
    UMM_TIME_STAT id_no_tag;
};
#endif
/////////////////////////////////////////////////
#ifdef DEBUG_ESP_OOM

#define MEMLEAK_DEBUG

// umm_*alloc are not renamed to *alloc
// Assumes umm_malloc.h has already been included.

#define umm_zalloc(s) umm_calloc(1,s)

void *malloc_loc(size_t s, const char *file, int line);
void *calloc_loc(size_t n, size_t s, const char *file, int line);
void *realloc_loc(void *p, size_t s, const char *file, int line);
// *alloc are macro calling *alloc_loc calling+checking umm_*alloc()
// they are defined at the bottom of this file

/////////////////////////////////////////////////

#elif defined(UMM_POISON_CHECK)
void *realloc_loc(void *p, size_t s, const char *file, int line);
void  free_loc(void *p, const char *file, int line);
#else // !defined(ESP_DEBUG_OOM)
#endif

#endif /* _UMM_MALLOC_CFGPORT_H */





#if defined(DEBUG_ESP_OOM) || defined(UMM_TAG_POISON_CHECK)
// this must be outside from "#ifndef _UMM_MALLOC_CFG_H"
// because Arduino.h's <cstdlib> does #undef *alloc
// Arduino.h recall us to redefine them
#include <pgmspace.h>
// Reuse pvPort* calls, since they already support passing location information.
// Specifically the debug version (heap_...) that does not force DRAM heap.
void *IRAM_ATTR heap_pvPortMalloc(size_t size, const char *file, int line);
void *IRAM_ATTR heap_pvPortCalloc(size_t count, size_t size, const char *file, int line);
void *IRAM_ATTR heap_pvPortRealloc(void *ptr, size_t size, const char *file, int line);
void *IRAM_ATTR heap_pvPortZalloc(size_t size, const char *file, int line);
void IRAM_ATTR heap_vPortFree(void *ptr, const char *file, int line);

#define malloc(s) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; heap_pvPortMalloc(s, mem_debug_file, __LINE__); })
#define calloc(n,s) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; heap_pvPortCalloc(n, s, mem_debug_file, __LINE__); })
#define realloc(p,s) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; heap_pvPortRealloc(p, s, mem_debug_file, __LINE__); })

#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK)
#define dbg_heap_free(p) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; heap_vPortFree(p, mem_debug_file, __LINE__); })
#else
#define dbg_heap_free(p) free(p)
#endif

#elif defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
#include <pgmspace.h>
void *IRAM_ATTR heap_pvPortRealloc(void *ptr, size_t size, const char *file, int line);
#define realloc(p,s) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; heap_pvPortRealloc(p, s, mem_debug_file, __LINE__); })

void IRAM_ATTR heap_vPortFree(void *ptr, const char *file, int line);
//C - to be discussed
/*
  Problem, I would like to report the file and line number with the umm poison
  event as close as possible to the event. The #define method works for malloc,
  calloc, and realloc those names are not as generic as free. A #define free
  captures too much. Classes with methods called free are included :(
  Inline functions would report the address of the inline function in the .h
  not where they are called.

  Anybody know a trick to make this work?

  Create dbg_heap_free() as an alternative for free() when you need a little
  more help in debugging the more challenging problems.
*/
#define dbg_heap_free(p) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; heap_vPortFree(p, mem_debug_file, __LINE__); })

#else
#define dbg_heap_free(p) free(p)
#endif /* DEBUG_ESP_OOM */

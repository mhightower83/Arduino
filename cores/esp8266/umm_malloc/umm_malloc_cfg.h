/*
 * Configuration for umm_malloc - target Arduino ESP8266 core
 */

#ifndef _UMM_MALLOC_CFG_H
#define _UMM_MALLOC_CFG_H

#include <debug.h>
#include <pgmspace.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <core_esp8266_features.h>
#include <stdlib.h>
#include <osapi.h>

#include "c_types.h"

/*
 * There are a number of defines you can set at compile time that affect how
 * the memory allocator will operate.
 * You can set them in your config file umm_malloc_cfg.h.
 * In GNU C, you also can set these compile time defines like this:
 *
 * -D UMM_TEST_MAIN
 *
 * Set this if you want to compile in the test suite
 *
 * -D UMM_BEST_FIT (defualt)
 *
 * Set this if you want to use a best-fit algorithm for allocating new
 * blocks
 *
 * -D UMM_FIRST_FIT
 *
 * Set this if you want to use a first-fit algorithm for allocating new
 * blocks
 *
 * -D UMM_DBG_LOG_LEVEL=n
 *
 * Set n to a value from 0 to 6 depending on how verbose you want the debug
 * log to be
 *
 * ----------------------------------------------------------------------------
 *
 * Support for this library in a multitasking environment is provided when
 * you add bodies to the UMM_CRITICAL_ENTRY and UMM_CRITICAL_EXIT macros
 * (see below)
 *
 * ----------------------------------------------------------------------------
 */

/////////////////////////////////////////////////
#ifdef DEBUG_ESP_OOM

#define MEMLEAK_DEBUG

// umm_*alloc are not renamed to *alloc
// Assumes umm_malloc.h has already been included.

#define umm_zalloc(s) umm_calloc(1,s)

void* malloc_loc (size_t s, const char* file, int line);
void* calloc_loc (size_t n, size_t s, const char* file, int line);
void* realloc_loc (void* p, size_t s, const char* file, int line);

// *alloc are macro calling *alloc_loc calling+checking umm_*alloc()
// they are defined at the bottom of this file

/////////////////////////////////////////////////
#else // !defined(ESP_DEBUG_OOM)
#endif


/* Start addresses and the size of the heap */
extern char _heap_start[];
#define UMM_MALLOC_CFG_HEAP_ADDR   ((uint32_t)&_heap_start)
#define UMM_MALLOC_CFG_HEAP_SIZE   ((size_t)(0x3fffc000 - UMM_MALLOC_CFG_HEAP_ADDR))

/* A couple of macros to make packing structures less compiler dependent */

#define UMM_H_ATTPACKPRE
#define UMM_H_ATTPACKSUF __attribute__((__packed__))

#define UMM_BEST_FIT
#undef  UMM_FIRST_FIT

/*
 * -D UMM_INFO :
 *
 * Enables a dup of the heap contents and a function to return the total
 * heap size that is unallocated - note this is not the same as the largest
 * unallocated block on the heap!
 */

#define UMM_INFO

#ifdef UMM_INFO
  typedef struct UMM_HEAP_INFO_t {
    unsigned short int totalEntries;
    unsigned short int usedEntries;
    unsigned short int freeEntries;

    unsigned short int totalBlocks;
    unsigned short int usedBlocks;
    unsigned short int freeBlocks;

    unsigned short int maxFreeContiguousBlocks;

    unsigned int freeSize2;
  }
  UMM_HEAP_INFO;

  extern UMM_HEAP_INFO ummHeapInfo;

  void ICACHE_FLASH_ATTR *umm_info( void *ptr, int force );
  size_t ICACHE_FLASH_ATTR umm_free_heap_size_info( void );
  size_t ICACHE_FLASH_ATTR umm_max_block_size( void );
  size_t ICACHE_FLASH_ATTR umm_block_size( void );

#else
#endif

/*
 * -D UMM_STATS :
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
 */

#define UMM_STATS

#ifdef UMM_STATS

typedef struct UMM_STATISTICS_t {
  unsigned short int free_blocks;
  unsigned short int free_blocks_min;
  size_t alloc_max_size;
  size_t oom_count;
}
UMM_STATISTICS;
extern UMM_STATISTICS ummStats;

size_t ICACHE_FLASH_ATTR umm_free_heap_size_lw( void );
size_t ICACHE_FLASH_ATTR umm_free_heap_size_lw_min( void );
size_t ICACHE_FLASH_ATTR umm_free_heap_size_min_reset( void );

inline size_t umm_get_max_alloc_size( void ) {
  return ummStats.alloc_max_size;
}

inline size_t umm_get_oom_count( void ) {
  return ummStats.oom_count;
}

inline size_t umm_free_heap_size_min( void ) {
  return umm_free_heap_size_lw_min();
}
#else
#endif

#ifdef UMM_INFO
inline size_t umm_free_heap_size( void ) {
  return umm_free_heap_size_info();
}
#else
inline size_t umm_free_heap_size( void ) {
  return umm_free_heap_size_lw();
}
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
 * were disabled at request time. Note, for realloc MAX disabled time will not
 * include the time from calling malloc and/or free when UMM_LIGHTWEIGHT_CPU is
 * defined. Examine code for specifics on what info is available and how to
 * access.
*/
#define UMM_CRITICAL_METRICS

#if defined(UMM_CRITICAL_METRICS)
// This option adds support for gathering time locked data
typedef struct UMM_TIME_STAT_t {
  uint32_t min;
  uint32_t max;
  uint32_t start;
  uint32_t intlevel;
}
UMM_TIME_STAT;

typedef struct UMM_TIME_STATS_t {
  UMM_TIME_STAT id_malloc;
  UMM_TIME_STAT id_realloc;
  UMM_TIME_STAT id_free;
  UMM_TIME_STAT id_info;
  UMM_TIME_STAT id_no_tag;
}
UMM_TIME_STATS;

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
  if (elapse < p->min)
      p->min = elapse;

  if (elapse > p->max)
      p->max = elapse;

  xt_wsr_ps(*saved_ps);
}
#endif

/*
 * A couple of macros to make it easier to protect the memory allocator
 * in a multitasking system. You should set these macros up to use whatever
 * your system uses for this purpose. You can disable interrupts entirely, or
 * just disable task switching - it's up to you
 *
 * NOTE WELL that these macros MUST be allowed to nest, because umm_free() is
 * called from within umm_malloc()
 */

 #if defined(UMM_CRITICAL_METRICS)
 #define UMM_CRITICAL_DECL(tag) uint32_t _saved_ps_##tag
 #define UMM_CRITICAL_ENTRY(tag) _critical_entry(&time_stats.tag, &_saved_ps_##tag)
 #define UMM_CRITICAL_EXIT(tag) _critical_exit(&time_stats.tag, &_saved_ps_##tag)

 #else  // ! UMM_CRITICAL_METRICS
 // This method preserves the intlevel on entry and restores the
 // original intlevel at exit.
 #define UMM_CRITICAL_DECL(tag) uint32_t _saved_ps_##tag
 #define UMM_CRITICAL_ENTRY(tag) _saved_ps_##tag = xt_rsil(DEFAULT_CRITICAL_SECTION_INTLEVEL)
 #define UMM_CRITICAL_EXIT(tag) xt_wsr_ps(_saved_ps_##tag)
 #endif

//D #define UMM_CRITICAL_DECL() UMM_CRITICAL_DECL(id_no_tag)
//D #define UMM_CRITICAL_ENTRY() UMM_CRITICAL_ENTRY(id_no_tag)
//D #define UMM_CRITICAL_EXIT() UMM_CRITICAL_EXIT(id_no_tag)

 /*
  * -D UMM_LIGHTWEIGHT_CPU
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
  * Also, using these macros will relieve the nesting requirement.
  *
  * These macros should probably not be used on multicore CPUs. Hardware locking
  * methods sometimes carry higher overhead and may not be suitable for frequent
  * calling.
  */

 #define UMM_LIGHTWEIGHT_CPU

 #ifdef UMM_LIGHTWEIGHT_CPU
 #define UMM_CRITICAL_SUSPEND(tag) UMM_CRITICAL_EXIT(tag)
 #define UMM_CRITICAL_RESUME(tag) UMM_CRITICAL_ENTRY(tag)
 #else
 #define UMM_CRITICAL_SUSPEND(tag)
 #define UMM_CRITICAL_RESUME(tag)
 #endif

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

// #define UMM_INTEGRITY_CHECK

#ifdef UMM_INTEGRITY_CHECK
   int umm_integrity_check( void );
#  define INTEGRITY_CHECK() umm_integrity_check()
   extern void umm_corruption(void);
#  define UMM_HEAP_CORRUPTION_CB() printf( "Heap Corruption!" )
#else
#  define INTEGRITY_CHECK() 0
#endif


/////////////////////////////////////////////////

#if defined(DEBUG_ESP_PORT) || defined(DEBUG_ESP_CORE)
#define UMM_POISON_CHECK
#endif

#if defined(UMM_POISON_CHECK)
#if !defined(DBGLOG_LEVEL) || DBGLOG_LEVEL < 3
// All debug prints in UMM_POISON_CHECK are level 3
#unset DBGLOG_LEVEL
#define DBGLOG_LEVEL 3
#endif
#endif

//+ TODO: support printing +//
#undef DBGLOG_FUNCTION
#define DBGLOG_FUNCTION local_printf
#define local_printf(...)


/////////////////////////////////////////////////

/*
 * -D UMM_POISON_CHECK :
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
 *      Number of poison bytes before each block, e.g. 2
 *    UMM_POISON_SIZE_AFTER:
 *      Number of poison bytes after each block e.g. 2
 *    UMM_POISONED_BLOCK_LEN_TYPE
 *      Type of the exact buffer length, e.g. `short`
 *
 * NOTE: each allocated buffer is aligned by 4 bytes. But when poisoning is
 * enabled, actual pointer returned to user is shifted by
 * `(sizeof(UMM_POISONED_BLOCK_LEN_TYPE) + UMM_POISON_SIZE_BEFORE)`.
 * It's your responsibility to make resulting pointers aligned appropriately.
 *
 * If poison corruption is detected, the message is printed and user-provided
 * callback is called: `UMM_HEAP_CORRUPTION_CB()`
 */

#define UMM_POISON_SIZE_BEFORE 4
#define UMM_POISON_SIZE_AFTER 4
#define UMM_POISONED_BLOCK_LEN_TYPE uint32_t

#ifdef UMM_POISON_CHECK
   void *umm_poison_malloc( size_t size );
   void *umm_poison_calloc( size_t num, size_t size );
   void *umm_poison_realloc( void *ptr, size_t size );
   void  umm_poison_free( void *ptr );
   int   umm_poison_check( void );
#  define POISON_CHECK() umm_poison_check()
#else
#  define POISON_CHECK() 0
#endif

#define UMM_HEAP_CORRUPTION_CB() panic()

#ifdef __cplusplus
}
#endif

#endif /* _UMM_MALLOC_CFG_H */

#ifdef DEBUG_ESP_OOM
// this must be outside from "#ifndef _UMM_MALLOC_CFG_H"
// because Arduino.h's <cstdlib> does #undef *alloc
// Arduino.h recall us to redefine them
#include <pgmspace.h>
#define malloc(s) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; malloc_loc(s, mem_debug_file, __LINE__); })
#define calloc(n,s) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; calloc_loc(n, s, mem_debug_file, __LINE__); })
#define realloc(p,s) ({ static const char mem_debug_file[] PROGMEM STORE_ATTR = __FILE__; realloc_loc(p, s, mem_debug_file, __LINE__); })
#endif /* DEBUG_ESP_OOM */

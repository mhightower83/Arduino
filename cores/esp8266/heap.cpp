/* heap.c - overrides of SDK heap handling functions
 * Copyright (c) 2016 Ivan Grokhotkov. All rights reserved.
 * This file is distributed under MIT license.
 */

/*
 * When this module was started, it was a simple redirect to umm_malloc heap
 * APIs for pvPortMalloc, ... and _malloc_r, ... familys of heap APIs. Now it is
 * either a thin wrapper for umm (as it was in the past) or it is a thick
 * wrapper acting as a convergence point) to capture heap debug info and/or
 * perform heap diagnostics.
 *
 * For the case of OOM, wrappers are always checking for an out of memory result
 * from malloc, calloc, and realloc. For OOM, the last caller address is always
 * saved. This address is report by Postmortem.
 *
 * Iventory of debug options that run from here
 *
 *  * DEBUG_ESP_OOM - Monitors malloc, calloc, and realloc for failure and
 *    saves the last failure for postmortem to display. Additionally if system
 *    OS print is enabled (system_get_os_print == true), a diagnostic message
 *    can be printed at the time of the OOM event. To furthor assist in
 *    debugging, "fancy macros" redefine malloc, calloc, and realloc to their
 *    matching cousins in the portable malloc family, These allow the capturing
 *    of the file name and line number of the caller that had the OOM event.
 *
 *  * UMM_POISON_CHECK - Adds and presets an extra 4 bytes at the beginning and
 *    end of the of each allocations. At each Heap API call, a complete sweep
 *    though every allocation is performed verifying that all poison is set.
 *    This option is available, but must be enabled through build defines.
 *    Arduino IDE Tools does not offer it as an option.
 *
 *  * UMM_POISON_CHECK_LITE - A much lighter version of UMM_POISON_CHECK.
 *    At each free and realloc, the current allocation is tested, then each
 *    active allocation neighbor is tested. This option is used when
 *    Tools->Debug: Serial is selected or Tools->Debug level: "CORE" is
 *    selected. While coverage is not 100%, a sketch is less likely to have
 *    strange behavior from heavy heap access.
 *
 *  * UMM_INTEGRITY_CHECK - Verify that the Heap is semantically correct. Checks
 *    that all of the block indexes make sense. Slows execution dramatically but
 *    catches errors really quickly. This option is available, but must be
 *    enabled through build defines. Arduino IDE Tools does not offer it as an
 *    option.
 *
 */

#include <stdlib.h>
#include "umm_malloc/umm_malloc.h"
extern "C" size_t umm_umul_sat(const size_t a, const size_t b);;

// Need FORCE_ALWAYS_INLINE to put HeapSelect class constructor/deconstructor in IRAM
#define FORCE_ALWAYS_INLINE_HEAP_SELECT
#include "umm_malloc/umm_heap_select.h"

#include <c_types.h>
#include <sys/reent.h>
#include <user_interface.h>

extern "C" {

///////////////////////////////////////////////////////////////////////////////
// Selecting from various heap function renames that facilitate inserting debug
// wrappers, and tradditional names for the non-debug case.
//

/*
 * With any debug options listed above, umm_malloc changes its heap API names
 * from malloc, calloc, realloc, and free to umm_malloc, umm_calloc,
 * umm_realloc,  and umm_free.
 *
 */
#undef STATIC_ALWAYS_INLINE
#undef ENABLE_THICK_DEBUG_WRAPPERS

#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
/*
 * With either of these defines, umm_malloc will build with umm_poison_*
 * wrappers for each Heap API.
 * Debug wrappers that need to include handling poison
 */
#define UMM_MALLOC(s)           umm_poison_malloc(s)
#define UMM_CALLOC(n,s)         umm_poison_calloc(n,s)
#define UMM_REALLOC_FL(p,s,f,l) umm_poison_realloc_cfl(p,s,__builtin_return_address(0),f,l)
#define UMM_FREE_FL(p,f,l)      umm_poison_free_cfl(p,__builtin_return_address(0),f,l)
#define STATIC_ALWAYS_INLINE
#define ENABLE_THICK_DEBUG_WRAPPERS

#undef realloc
#undef free

#elif defined(DEBUG_ESP_OOM) || defined(UMM_INTEGRITY_CHECK)
// All other debug wrappers that do not require handling poison
#define UMM_MALLOC(s)           umm_malloc(s)
#define UMM_CALLOC(n,s)         umm_calloc(n,s)
#define UMM_REALLOC_FL(p,s,f,l) umm_realloc(p,s)
#define UMM_FREE_FL(p,f,l)      umm_free(p)
#define STATIC_ALWAYS_INLINE
#define ENABLE_THICK_DEBUG_WRAPPERS

#undef realloc
#undef free

#else  // ! UMM_POISON_CHECK && ! DEBUG_ESP_OOM
// Used to create thin heap wrappers not for debugging.
#define UMM_MALLOC(s)           malloc(s)
#define UMM_CALLOC(n,s)         calloc(n,s)
#define UMM_REALLOC_FL(p,s,f,l) realloc(p,s)
#define UMM_FREE_FL(p,f,l)      free(p)

// STATIC_ALWAYS_INLINE only applies to the non-debug build path,
// it must not be enabled on the debug build path.
#define STATIC_ALWAYS_INLINE static ALWAYS_INLINE
#endif


///////////////////////////////////////////////////////////////////////////////
// UMM_POISON_CHECK wrapper macros
//
#if defined(UMM_POISON_CHECK)
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

#else
  // Disable full heap poison checking.
  #define POISON_CHECK__ABORT() do {} while(0)
  #define POISON_CHECK__PANIC_FL(file, line) do { (void)file; (void)line; } while(0)
#endif


///////////////////////////////////////////////////////////////////////////////
// UMM_INTEGRITY_CHECK wrapper macros
//
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


///////////////////////////////////////////////////////////////////////////////
// OOM - these variables are always in use by abi.cpp
//
// Always track last failed caller and size requested
void *umm_last_fail_alloc_addr = NULL;
size_t umm_last_fail_alloc_size = 0u;
#if defined(DEBUG_ESP_OOM)
const char *umm_last_fail_alloc_file = NULL;
int umm_last_fail_alloc_line = 0;
#endif


///////////////////////////////////////////////////////////////////////////////
// OOM - DEBUG_ESP_OOM extends monitoring for OOM to capture caller information
// across various Heap entry points and their aliases.
//
// data capture wrapper macros and defines
// Debugging helper, save the last caller address that got a NULL pointer
// response. And when available, the file and line number.
#if defined(DEBUG_ESP_OOM)
#define PTR_CHECK__LOG_LAST_FAIL_FL(p, s, f, l) \
    if(0 != (s) && 0 == p)\
    {\
      umm_last_fail_alloc_addr = __builtin_return_address(0);\
      umm_last_fail_alloc_size = s;\
      umm_last_fail_alloc_file = f;\
      umm_last_fail_alloc_line = l;\
    }
#define PTR_CHECK__LOG_LAST_FAIL(p, s) \
    if(0 != (s) && 0 == p)\
    {\
      umm_last_fail_alloc_addr = __builtin_return_address(0);\
      umm_last_fail_alloc_size = s;\
      umm_last_fail_alloc_file = NULL;\
      umm_last_fail_alloc_line = 0;\
    }
#else
// These are targeted at LIBC, always capture minimum OOM details
#define PTR_CHECK__LOG_LAST_FAIL_FL(p, s, f, l) \
    (void)f;\
    (void)l;\
    if(0 != (s) && 0 == p)\
    {\
      umm_last_fail_alloc_addr = __builtin_return_address(0);\
      umm_last_fail_alloc_size = s;\
    }
#define PTR_CHECK__LOG_LAST_FAIL(p, s) \
    if(0 != (s) && 0 == p)\
    {\
      umm_last_fail_alloc_addr = __builtin_return_address(0);\
      umm_last_fail_alloc_size = s;\
    }
#endif

///////////////////////////////////////////////////////////////////////////////
// overrides/thin wrapper functions for libc Heap calls
// Note these always have at least the lite version of PTR_CHECK__LOG_LAST_FAIL
// monitoring in place.
void* _malloc_r(struct _reent* unused, size_t size)
{
    (void) unused;
    void *ret = malloc(size);
    PTR_CHECK__LOG_LAST_FAIL(ret, size);
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
    PTR_CHECK__LOG_LAST_FAIL(ret, size);
    return ret;
}

void* _calloc_r(struct _reent* unused, size_t count, size_t size)
{
    (void) unused;
    void *ret = calloc(count, size);
    PTR_CHECK__LOG_LAST_FAIL(ret, umm_umul_sat(count, size));
    return ret;
}

///////////////////////////////////////////////////////////////////////////////
// OOM - Debug printing macros
//
// IRQ/ISR safe printing macros. Printing is controled according to the results
// of system_get_os_print(). Also, being in a IRQ will prevent the printing of
// file names stored in PROGMEM. The PROGMEM address to the string is printed in
// its place.
//
#ifdef DEBUG_ESP_OOM
#undef malloc
#undef calloc
#undef realloc

#define DEBUG_HEAP_PRINTF ets_uart_printf

void IRAM_ATTR print_loc(size_t size, const char* file, int line)
{
    (void)size;
    (void)line;
    if (system_get_os_print()) {
        DEBUG_HEAP_PRINTF(":oom(%d)@", (int)size);

        bool inISR = ETS_INTR_WITHINISR();
        if (inISR && (uint32_t)file >= 0x40200000) {
            DEBUG_HEAP_PRINTF("File: %p", file);
        } else if (!inISR && (uint32_t)file >= 0x40200000) {
            char buf[strlen_P(file) + 1];
            strcpy_P(buf, file);
            DEBUG_HEAP_PRINTF(buf);
        } else {
            DEBUG_HEAP_PRINTF(file);
        }

        DEBUG_HEAP_PRINTF(":%d\n", line);
    }
}

void IRAM_ATTR print_oom_size(size_t size)
{
    (void)size;
    if (system_get_os_print()) {
        DEBUG_HEAP_PRINTF(":oom(%d)@?\n", (int)size);
    }
}

#define OOM_CHECK__PRINT_OOM(p, s) if ((s) && !(p)) print_oom_size(s)
#define OOM_CHECK__PRINT_LOC(p, s, f, l) if ((s) && !(p)) print_loc(s, f, l)

#else
// We are finished with LIBC alloc functions. From here down, no OOM logging.
#undef PTR_CHECK__LOG_LAST_FAIL_FL
#define PTR_CHECK__LOG_LAST_FAIL_FL(p, s, f, l)
#undef PTR_CHECK__LOG_LAST_FAIL
#define PTR_CHECK__LOG_LAST_FAIL(p, s)

#define OOM_CHECK__PRINT_OOM(p, s)
#define OOM_CHECK__PRINT_LOC(p, s, f, l)
#endif



///////////////////////////////////////////////////////////////////////////////
// UMM_POINTER_CHECK wrapper macros
//
#if (UMM_POINTER_CHECK >= 2)
#define PTR_CHECK__LOG_INVALID_FL(p, f, l) \
    umm_pointer_check_wrap(p, __builtin_return_address(0), f, l)

#define PTR_CHECK__LOG_INVALID(p) \
    umm_pointer_check_wrap(p, __builtin_return_address(0), NULL, 0)

#else
#define PTR_CHECK__LOG_INVALID_FL(p, f, l)
#define PTR_CHECK__LOG_INVALID(p)
#endif


///////////////////////////////////////////////////////////////////////////////
// Thick heap API wrapper for debugging - calloc, malloc, realloc, and free
//
#ifdef ENABLE_THICK_DEBUG_WRAPPERS
/*
   Side notes, UMM_INTEGRITY_CHECK and UMM_POISON_CHECK are coded for; however,
   the Arduino IDE has no selection to build with them. Both are CPU intensive
   and can adversly effect the WiFi operation. We have option
   UMM_POISON_CHECK_LITE to replace UMM_POISON_CHECK. This is include in
   the debug build when you select the Debug Port. For completeness they are all
   included in the list below. Both UMM_INTEGRITY_CHECK and UMM_POISON_CHECK
   can be enabled by a build define.

  The thinking behind the ordering of Integrity Check, Full Poison Check, and
  the specific *alloc function.

  1. Integrity Check - verifies the heap management information is not corrupt.
     This allows any other testing, that walks the heap, to run safely.

  2. Place Full Poison Check before or after a specific *alloc function?
     a. After, when the *alloc function operates on an existing allocation.
     b. Before, when the *alloc function creates a new, not modified, allocation.

     In a free() or realloc() call, the focus is on their allocation. It is
     checked 1st and reported on 1ST if an error exists. Full Poison Check is
     done after.

     For malloc(), calloc(), and zalloc() Full Poison Check is done 1st since
     these functions do not modify an existing allocation.
*/
#undef malloc
#undef calloc
#undef realloc
#undef free

void* IRAM_ATTR malloc(size_t size)
{
    INTEGRITY_CHECK__ABORT();
    POISON_CHECK__ABORT();
    void* ret = UMM_MALLOC(size);
    PTR_CHECK__LOG_LAST_FAIL(ret, size);
    OOM_CHECK__PRINT_OOM(ret, size);
    return ret;
}

void* IRAM_ATTR calloc(size_t count, size_t size)
{
    INTEGRITY_CHECK__ABORT();
    POISON_CHECK__ABORT();
    void* ret = UMM_CALLOC(count, size);
    #if defined(DEBUG_ESP_OOM)
    size_t total_size = umm_umul_sat(count, size);// For logging purposes
    #endif
    PTR_CHECK__LOG_LAST_FAIL(ret, total_size);
    OOM_CHECK__PRINT_OOM(ret, total_size);
    return ret;
}

void* IRAM_ATTR realloc(void* ptr, size_t size)
{
    INTEGRITY_CHECK__ABORT();
    PTR_CHECK__LOG_INVALID(ptr);
    void* ret = UMM_REALLOC_FL(ptr, size, NULL, 0);
    POISON_CHECK__ABORT();
    PTR_CHECK__LOG_LAST_FAIL(ret, size);
    OOM_CHECK__PRINT_OOM(ret, size);
    return ret;
}

void IRAM_ATTR free(void* ptr)
{
    INTEGRITY_CHECK__ABORT();
    PTR_CHECK__LOG_INVALID(ptr);
    UMM_FREE_FL(ptr, NULL, 0);
    POISON_CHECK__ABORT();
}
#endif

///////////////////////////////////////////////////////////////////////////////
// Thick portable malloc API (pvPortMalloc, ...) heapers and wrapper for debugging
//
// STATIC_ALWAYS_INLINE - is only defined as such on non-debug builds to make the
// wrappers as thin as possible for performance. For Debug builds we want these
// helper functions to be separate.
//
// For debug builds, these helpers allow us to correctly capture the actual
// caller's address. These are used by the "fancy macros" defined in
// heap_api_debug.h that identify the caller's file name and line number.
//
STATIC_ALWAYS_INLINE
void* IRAM_ATTR heap_pvPortMalloc(size_t size, const char* file, int line)
{
    INTEGRITY_CHECK__PANIC_FL(file, line);
    POISON_CHECK__PANIC_FL(file, line);
    void* ret = UMM_MALLOC(size);
    PTR_CHECK__LOG_LAST_FAIL_FL(ret, size, file, line);
    OOM_CHECK__PRINT_LOC(ret, size, file, line);
    return ret;
}

STATIC_ALWAYS_INLINE
void* IRAM_ATTR heap_pvPortCalloc(size_t count, size_t size, const char* file, int line)
{
    INTEGRITY_CHECK__PANIC_FL(file, line);
    POISON_CHECK__PANIC_FL(file, line);
    void* ret = UMM_CALLOC(count, size);
    #if defined(DEBUG_ESP_OOM)
    size_t total_size = umm_umul_sat(count, size);
    #endif
    PTR_CHECK__LOG_LAST_FAIL_FL(ret, total_size, file, line);
    OOM_CHECK__PRINT_LOC(ret, total_size, file, line);
    return ret;
}

STATIC_ALWAYS_INLINE
void* IRAM_ATTR heap_pvPortRealloc(void *ptr, size_t size, const char* file, int line)
{
    INTEGRITY_CHECK__PANIC_FL(file, line);
    PTR_CHECK__LOG_INVALID_FL(ptr, file, line);
    void* ret = UMM_REALLOC_FL(ptr, size, file, line);
    POISON_CHECK__PANIC_FL(file, line);
    PTR_CHECK__LOG_LAST_FAIL_FL(ret, size, file, line);
    OOM_CHECK__PRINT_LOC(ret, size, file, line);
    return ret;
}

STATIC_ALWAYS_INLINE
void* IRAM_ATTR heap_pvPortZalloc(size_t size, const char* file, int line)
{
    INTEGRITY_CHECK__PANIC_FL(file, line);
    POISON_CHECK__PANIC_FL(file, line);
    void* ret = UMM_CALLOC(1, size);
    PTR_CHECK__LOG_LAST_FAIL_FL(ret, size, file, line);
    OOM_CHECK__PRINT_LOC(ret, size, file, line);
    return ret;
}

STATIC_ALWAYS_INLINE
void IRAM_ATTR heap_vPortFree(void *ptr, const char* file, int line)
{
    INTEGRITY_CHECK__PANIC_FL(file, line);
    PTR_CHECK__LOG_INVALID_FL(ptr, file, line);
    UMM_FREE_FL(ptr, file, line);
    POISON_CHECK__PANIC_FL(file, line);
}


///////////////////////////////////////////////////////////////////////////////
// overrides/thin wrapper functions for the NONOS SDK and lwIP Heap API calls
//
size_t IRAM_ATTR xPortWantedSizeAlign(size_t size)
{
    return (size + 3) & ~((size_t) 3);
}

void system_show_malloc(void)
{
    HeapSelectDram ephemeral;
    umm_info(NULL, true);
}

/*
  NONOS SDK and lwIP do not handle IRAM heap well. Since they also use portable
  malloc calls pvPortMalloc, ... we can leverage that for this solution.
  Force pvPortMalloc, ... APIs to serve DRAM only.
*/
void* IRAM_ATTR pvPortMalloc(size_t size, const char* file, int line)
{
    HeapSelectDram ephemeral;
    return heap_pvPortMalloc(size,  file, line);;
}

void* IRAM_ATTR pvPortCalloc(size_t count, size_t size, const char* file, int line)
{
    HeapSelectDram ephemeral;
    return heap_pvPortCalloc(count, size,  file, line);
}

void* IRAM_ATTR pvPortRealloc(void *ptr, size_t size, const char* file, int line)
{
    HeapSelectDram ephemeral;
    return heap_pvPortRealloc(ptr, size,  file, line);
}

void* IRAM_ATTR pvPortZalloc(size_t size, const char* file, int line)
{
    HeapSelectDram ephemeral;
    return heap_pvPortZalloc(size,  file, line);
}

void IRAM_ATTR vPortFree(void *ptr, const char* file, int line)
{
#if defined(UMM_POISON_CHECK) || defined(UMM_INTEGRITY_CHECK)
    // While umm_free internally determines the correct heap, UMM_POISON_CHECK
    // and UMM_INTEGRITY_CHECK do not have arguments. They have to rely on the
    // current heap to identify which one to analyze.
    //
    // Should not need this for UMM_POISON_CHECK_LITE, it directly handles
    // multiple heaps. DEBUG_ESP_OOM not tied to any one heap.
    HeapSelectDram ephemeral;
#endif
    return heap_vPortFree(ptr,  file, line);
}

};

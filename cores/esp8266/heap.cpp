/* heap.c - overrides of SDK heap handling functions
 * Copyright (c) 2016 Ivan Grokhotkov. All rights reserved.
 * This file is distributed under MIT license.
 */
/*
 *
 * When this module started, it was a simple convergence point and redirect to
 * umm_malloc heap APIs for pvPortMalloc, ... and _malloc_r, ... families of
 * heap APIs. Now it is either a thin wrapper or a thick wrapper acting as a
 * convergence point to capture heap debug info and diagnostics.
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
 *    For the case of OOM via LIBC, wrappers are always checking for an out of
 *    memory result, The last caller address is always saved. This address may
 *    be report by Postmortem.
 *
 *  * UMM_POISON_CHECK - Adds and presets an extra 4 bytes at the beginning and
 *    end of each allocations. At each Heap API call: a complete sweep through
 *    every "allocation" is performed, verifying that all poison is set. This
 *    option is available but must be enabled through build defines. This option
 *    will cause increasingly long periods with interrupts disabled as the
 *    number of active heap allocations grows. This may adversely affect
 *    time-critical sketches. UMM_POISON_CHECK_LITE is a better choice. Arduino
 *    IDE Tools does not offer it as an option.
 *
 *  * UMM_POISON_CHECK_LITE - A much lighter version of UMM_POISON_CHECK.
 *    At each free and realloc, the current allocation is tested, then each
 *    active allocation neighbor is tested. This option is used when
 *    Tools->Debug: Serial is selected or Tools->Debug level: "CORE" is
 *    selected. While coverage is not 100%, a sketch is less likely to have
 *    strange behavior from heavy heap access.
 *
 *  * UMM_INTEGRITY_CHECK - will verify that the Heap is semantically correct
 *    and that all the block indexes make sense. It will slow execution
 *    dramatically; however, it catches errors quickly. Use build defines to
 *    enable this option; Arduino IDE Tools does not offer it as an option.
 *
 *    IMHO, UMM_INTEGRITY_CHECK is best suited for heap library verification
 *    rather than general debugging. An optimum choice UMM_POISON_CHECK_LITE
 *    will likely detect most heap corruptions with lower overhead.
 */

#include <stdlib.h>
#include "umm_malloc/umm_malloc.h"
/*
 * HEAP_USER_BREAK_FLC_CB - A debug build option. User-defined "C" function
 * callback for Heap related failures. It can range from a simple breakpoint to
 * a complicated backtrace or other supplemental information. The callback
 * context is very restrictive: no Heap calls, no PROGMEM, IRAM code only, no
 * flash strings, and limited to ROM-based printf functions (like
 * ets_uart_printf(...)).
 *
 * Example:
 *   extern "C" void my_user_bp(const char* f, int l, const void* c) {
 *     (void)f; (void)l; (void)c;
 *     __asm__ __volatile__("break 1, 0;" ::: "memory");
 *   }
 *
 *   -DHEAP_USER_BREAK_FLC_CB='my_user_bp'
 */
#ifdef HEAP_USER_BREAK_FLC_CB
extern "C" void HEAP_USER_BREAK_FLC_CB(const char* file, int line, const void *caller);
#define _HEAP_USER_BREAK_FLC_CB HEAP_USER_BREAK_FLC_CB
#else
#define _HEAP_USER_BREAK_FLC_CB(f, l, c) do { (void)f; (void)l; (void)c; } while(0)
#endif

extern "C" size_t umm_umul_sat(const size_t a, const size_t b);

// z2EapFree: See wpa2_eap_patch.cpp for details
extern "C" void z2EapFree(void *ptr, const char* file, int line) __attribute__((weak, alias("vPortFree"), nothrow));
// I don't understand all the compiler noise around this alias.
// Adding "__attribute__ ((nothrow))" seems to resolve the issue.
// This may be relevant: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=81824

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
#define UMM_MALLOC_FL(s,f,l,c)    umm_poison_malloc(s)
#define UMM_CALLOC_FL(n,s,f,l,c)  umm_poison_calloc(n,s)
#define UMM_ZALLOC_FL(s,f,l,c)    umm_poison_calloc(1,s)
#define UMM_REALLOC_FL(p,s,f,l,c) umm_poison_realloc_flc(p,s,f,l,c)
#define UMM_FREE_FL(p,f,l,c)      umm_poison_free_flc(p,f,l,c)
#define ENABLE_THICK_DEBUG_WRAPPERS

#undef realloc
#undef free

#elif defined(DEBUG_ESP_OOM) || defined(UMM_INTEGRITY_CHECK)
// All other debug wrappers that do not require handling poison
#define UMM_MALLOC_FL(s,f,l,c)    umm_malloc(s)
#define UMM_CALLOC_FL(n,s,f,l,c)  umm_calloc(n,s)
#define UMM_ZALLOC_FL(s,f,l,c)    umm_calloc(1,s)
#define UMM_REALLOC_FL(p,s,f,l,c) umm_realloc(p,s)
#define UMM_FREE_FL(p,f,l,c)      umm_free(p)
#define ENABLE_THICK_DEBUG_WRAPPERS

#undef realloc
#undef free

#else  // ! UMM_POISON_CHECK && ! DEBUG_ESP_OOM
// Used to create thin heap wrappers not for debugging.
#define UMM_MALLOC(s)             malloc(s)
#define UMM_CALLOC(n,s)           calloc(n,s)
#define UMM_ZALLOC(s)             calloc(1,s)
#define UMM_REALLOC(p,s)          realloc(p,s)
#define UMM_FREE(p)               free(p)
// Keep a slightly thicker wrapper for ABI OOM handling
#define UMM_MALLOC_FL(s,f,l,c)    malloc(s)
#endif


///////////////////////////////////////////////////////////////////////////////
// UMM_POISON_CHECK wrapper macros
//
// Take care not to blame the messenger; the function (file/line) that resulted
// in the discovery may not be directly responsible for the damage. We could use
// abort; however, using panic may provide some hints of the location of the
// problem.
//
// The failure is a discovery of an error that could have occurred at any time
// between calls to POISON_CHECK.
//
#if defined(UMM_POISON_CHECK)
  #define POISON_CHECK__PANIC_FL(f, l, c) \
      do { \
          if ( ! POISON_CHECK() ) { \
              _HEAP_USER_BREAK_FLC_CB(f, l, c); \
              __panic_func(f, l, ""); \
          } \
      } while(0)

#else
  // Disable full heap poison checking.
  #define POISON_CHECK__PANIC_FL(f, l, c) do { (void)f; (void)l; (void)c; } while(0)
#endif


///////////////////////////////////////////////////////////////////////////////
// UMM_INTEGRITY_CHECK wrapper macros
//
// (Caution notes of UMM_POISON_CHECK also apply here.)
//
#ifdef UMM_INTEGRITY_CHECK
#define INTEGRITY_CHECK__PANIC_FL(f, l, c) \
    do { \
        if ( ! INTEGRITY_CHECK() ) { \
            _HEAP_USER_BREAK_FLC_CB(f, l, c); \
            __panic_func(f, l, ""); \
        } \
    } while(0)

#else  // ! UMM_INTEGRITY_CHECK
#define INTEGRITY_CHECK__PANIC_FL(f, l, c) do { (void)f; (void)l; (void)c; } while(0)
#endif //   UMM_INTEGRITY_CHECK


///////////////////////////////////////////////////////////////////////////////
// OOM - this structure variable is always in use by abi.cpp
//
// Always track last failed caller and size requested
//
// Update structure from a CRITICAL_SECTION to ensure detail changes are atomic.
// Otherwise, if interrupted at the wrong time, OOM details would mix between
// foreground and IRQ paths.
#if defined(DEBUG_ESP_OOM)
struct umm_last_fail_alloc {
    const void *addr;
    size_t size;
    const char *file;
    int line;
} _umm_last_fail_alloc = {NULL, 0, NULL, 0};

#else
struct umm_last_fail_alloc {
    const void *addr;
    size_t size;
} _umm_last_fail_alloc = {NULL, 0};
#endif


///////////////////////////////////////////////////////////////////////////////
// OOM - DEBUG_ESP_OOM extends monitoring for OOM to capture caller information
// across various Heap entry points and their aliases.
//
// data capture wrapper macros and defines
// Debugging helper, save the last caller address that got a NULL pointer
// response. And when available, the file and line number.
#if defined(DEBUG_ESP_OOM)

// OOM - Debug printing
//
// IRQ/ISR safe printing macros. Printing is controled according to the results
// of system_get_os_print(). Also, being in a IRQ will prevent the printing of
// file names stored in PROGMEM. The PROGMEM address to the string is printed in
// its place.
#define DEBUG_HEAP_PRINTF ets_uart_printf
static void IRAM_ATTR print_loc(size_t size, const char* file, int line, const void* caller)
{
    if (system_get_os_print()) {
        DEBUG_HEAP_PRINTF(":oom(%d)@", (int)size);

        if (file) {
            bool inISR = ETS_INTR_WITHINISR();
            if (inISR && (uint32_t)file >= 0x40200000) {
                DEBUG_HEAP_PRINTF("%p, File: %p", caller, file);
            } else if (!inISR && (uint32_t)file >= 0x40200000) {
                char buf[strlen_P(file) + 1];
                strcpy_P(buf, file);
                DEBUG_HEAP_PRINTF("%p, File: %s", caller, buf);
            } else {
                DEBUG_HEAP_PRINTF(file);
            }
            DEBUG_HEAP_PRINTF(":%d\n", line);
        } else {
            DEBUG_HEAP_PRINTF("%p\n", caller);
        }
    }
    _HEAP_USER_BREAK_FLC_CB(file, line, caller);
}

extern bool IRAM_ATTR oom_check__log_last_fail_fl(void *ptr, size_t size, [[maybe_unused]] const char* file, [[maybe_unused]] int line, const void* const caller) {
    if (0 != (size) && 0 == ptr) {
        uint32_t saved_ps = xt_rsil(DEFAULT_CRITICAL_SECTION_INTLEVEL);
        _umm_last_fail_alloc.addr = caller;
        _umm_last_fail_alloc.size = size;
        _umm_last_fail_alloc.file = file;
        _umm_last_fail_alloc.line = line;
        print_loc(s, f, l, c);
        xt_wsr_ps(saved_ps);
        return false;
    }
    return true;
}

#define OOM_CHECK__LOG_LAST_FAIL_FL(p, s, f, l, c) oom_check__log_last_fail_fl(p, s, f, l, c)
#define OOM_CHECK__LOG_LAST_FAIL(p, s, c) oom_check__log_last_fail_fl(p, s, NULL, 0, c)

#else
extern bool IRAM_ATTR oom_check__log_last_fail_fl(void *ptr, size_t size, const void* const caller) {
    if (0 != (size) && 0 == ptr) {
        uint32_t saved_ps = xt_rsil(DEFAULT_CRITICAL_SECTION_INTLEVEL);
        _umm_last_fail_alloc.addr = caller;
        _umm_last_fail_alloc.size = size;
        xt_wsr_ps(saved_ps);
        return false;
    }
    return true;
}
// These are targeted at LIBC, always capture minimum OOM details
#define OOM_CHECK__LOG_LAST_FAIL_FL(p, s, f, l, c) oom_check__log_last_fail_fl(p, s, c)
#define OOM_CHECK__LOG_LAST_FAIL(p, s, c) oom_check__log_last_fail_fl(p, s, c)
#endif


///////////////////////////////////////////////////////////////////////////////
// heap allocator for ABI - Always defined
void* IRAM_ATTR _heap_abi_malloc(size_t size, bool unhandle, const void* const caller)
{
    const char *file = NULL;
    const int line = 0;
    INTEGRITY_CHECK__PANIC_FL(file, line, caller);
    POISON_CHECK__PANIC_FL(file, line, caller);
    // The above checks go away for non-debug builds

    void* ret = UMM_MALLOC_FL(size, file, line, caller);
    // OOM check is always present
    if (!OOM_CHECK__LOG_LAST_FAIL(ret, size, caller) && unhandle) {
        __unhandled_exception(PSTR("OOM"));
    }
    return ret;
}

#ifndef ENABLE_THICK_DEBUG_WRAPPERS
///////////////////////////////////////////////////////////////////////////////
// For non-debug case the heap family of malloc, calloc, ... calls have no
// wrappers. They are directly defined in umm_malloc.
//
///////////////////////////////////////////////////////////////////////////////
// overrides/thin wrapper functions for libc Heap calls
// Note these always have at least the lite version of OOM_CHECK__LOG_LAST_FAIL
// monitoring in place.
// capture caller return address at exposed API
//
void* _malloc_r(struct _reent* unused, size_t size)
{
    (void) unused;
    void *ret = malloc(size);
    OOM_CHECK__LOG_LAST_FAIL(ret, size, __builtin_return_address(0));
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
    OOM_CHECK__LOG_LAST_FAIL(ret, size, __builtin_return_address(0));
    return ret;
}

void* _calloc_r(struct _reent* unused, size_t count, size_t size)
{
    (void) unused;
    void *ret = calloc(count, size);
    OOM_CHECK__LOG_LAST_FAIL(ret, umm_umul_sat(count, size), __builtin_return_address(0));
    return ret;
}
#endif

///////////////////////////////////////////////////////////////////////////////
//
#ifndef DEBUG_ESP_OOM
// We are finished with LIBC alloc functions. From here down, no OOM logging.
#undef OOM_CHECK__LOG_LAST_FAIL
#define OOM_CHECK__LOG_LAST_FAIL(p, s, c)
#endif

#ifdef ENABLE_THICK_DEBUG_WRAPPERS
///////////////////////////////////////////////////////////////////////////////
// Thick heap API wrapper for debugging - calloc, malloc, realloc, and free
//
// While UMM_INTEGRITY_CHECK and UMM_POISON_CHECK are included, the Arduino IDE
// has no selection to build with them. Both are CPU intensive and can adversly
// effect the WiFi operation. We use option UMM_POISON_CHECK_LITE instead of
// UMM_POISON_CHECK. This is include in the debug build when you select the
// Debug Port. For completeness they are all included in the list below. Both
// UMM_INTEGRITY_CHECK and UMM_POISON_CHECK can be enabled by a build define.
//
// The thinking behind the ordering of Integrity Check, Full Poison Check, and
// the specific *alloc function.
//
// 1. Integrity Check - verifies the heap management information is not corrupt.
//    This allows any other testing, that walks the heap, to run safely.
//
// 2. Place Full Poison Check before or after a specific *alloc function?
//    a. After, when the *alloc function operates on an existing allocation.
//    b. Before, when the *alloc function creates a new, not modified, allocation.
//
//    In a free() or realloc() call, the focus is on their allocation. It is
//    checked 1st and reported on 1ST if an error exists. Full Poison Check is
//    done after.
//
//    For malloc(), calloc(), and zalloc() Full Poison Check is done 1st since
//    these functions do not modify an existing allocation.
//
#undef malloc
#undef calloc
#undef realloc
#undef free

///////////////////////////////////////////////////////////////////////////////
// Common Heap debug helper functions for each alloc operation
//
// Used by debug wrapper for:
//   * portable malloc API, pvPortMalloc, ...
//   * LIBC variation, _malloc_r, ...
//   * "fancy macros" that call heap_pvPortMalloc, ...
//   * Fallback for uncapture malloc API calls, malloc, ...
//
void* IRAM_ATTR _heap_pvPortMalloc(size_t size, const char* file, int line, const void *caller)
{
    INTEGRITY_CHECK__PANIC_FL(file, line, caller);
    POISON_CHECK__PANIC_FL(file, line, caller);
    void* ret = UMM_MALLOC_FL(size, file, line, caller);
    OOM_CHECK__LOG_LAST_FAIL_FL(ret, size, file, line, caller);
    return ret;
}

void* IRAM_ATTR _heap_pvPortCalloc(size_t count, size_t size, const char* file, int line, const void *caller)
{
    INTEGRITY_CHECK__PANIC_FL(file, line, caller);
    POISON_CHECK__PANIC_FL(file, line, caller);
    void* ret = UMM_CALLOC_FL(count, size, file, line, caller);
    #if defined(DEBUG_ESP_OOM)
    size_t total_size = umm_umul_sat(count, size);
    #endif
    OOM_CHECK__LOG_LAST_FAIL_FL(ret, total_size, file, line, caller);
    return ret;
}

void* IRAM_ATTR _heap_pvPortZalloc(size_t size, const char* file, int line, const void *caller)
{
    INTEGRITY_CHECK__PANIC_FL(file, line, caller);
    POISON_CHECK__PANIC_FL(file, line, caller);
    void* ret = UMM_ZALLOC_FL(size, file, line, caller);
    OOM_CHECK__LOG_LAST_FAIL_FL(ret, size, file, line, caller);
    return ret;
}

void* IRAM_ATTR _heap_pvPortRealloc(void *ptr, size_t size, const char* file, int line, const void *caller)
{
    INTEGRITY_CHECK__PANIC_FL(file, line, caller);
    void* ret = UMM_REALLOC_FL(ptr, size, file, line, caller);
    POISON_CHECK__PANIC_FL(file, line, caller);
    OOM_CHECK__LOG_LAST_FAIL_FL(ret, size, file, line, caller);
    return ret;
}

void IRAM_ATTR _heap_vPortFree(void *ptr, const char* file, int line, [[maybe_unused]] const void *caller)
{
    INTEGRITY_CHECK__PANIC_FL(file, line, caller);
    UMM_FREE_FL(ptr, file, line, caller);
    POISON_CHECK__PANIC_FL(file, line, caller);
}

///////////////////////////////////////////////////////////////////////////////
// Heap debug wrappers used by "fancy debug macros" to capture caller's context:
// module name, line no. and caller return address.
// The "fancy debug macros" are defined in `heap_api_debug.h`
void* IRAM_ATTR heap_pvPortMalloc(size_t size, const char* file, int line)
{
    return _heap_pvPortMalloc(size,  file, line, __builtin_return_address(0));
}

void* IRAM_ATTR heap_pvPortCalloc(size_t count, size_t size, const char* file, int line)
{
    return _heap_pvPortCalloc(count, size,  file, line, __builtin_return_address(0));
}

void* IRAM_ATTR heap_pvPortZalloc(size_t size, const char* file, int line)
{
    return _heap_pvPortZalloc(size,  file, line, __builtin_return_address(0));
}

void* IRAM_ATTR heap_pvPortRealloc(void *ptr, size_t size, const char* file, int line)
{
    return _heap_pvPortRealloc(ptr, size,  file, line, __builtin_return_address(0));
}

void IRAM_ATTR heap_vPortFree(void *ptr, const char* file, int line)
{
    return _heap_vPortFree(ptr, file, line, __builtin_return_address(0));
}

///////////////////////////////////////////////////////////////////////////////
// Heap debug wrappers used by LIBC
// capture caller return address at exposed API
void* _malloc_r(struct _reent* unused, size_t size)
{
    (void) unused;
    return _heap_pvPortMalloc(size, NULL, 0, __builtin_return_address(0));
}

void* _calloc_r(struct _reent* unused, size_t count, size_t size)
{
    (void) unused;
    return _heap_pvPortCalloc(count, size, NULL, 0, __builtin_return_address(0));
}

void* _realloc_r(struct _reent* unused, void* ptr, size_t size)
{
    (void) unused;
    return _heap_pvPortRealloc(ptr, size, NULL, 0, __builtin_return_address(0));
}

void _free_r(struct _reent* unused, void* ptr)
{
    (void) unused;
    _heap_vPortFree(ptr, NULL, 0, __builtin_return_address(0));
}

///////////////////////////////////////////////////////////////////////////////
// Heap debug wrappers used to captured any remaining standard heap api calls
void* IRAM_ATTR malloc(size_t size)
{
    return _heap_pvPortMalloc(size, NULL, 0, __builtin_return_address(0));
}

void* IRAM_ATTR calloc(size_t count, size_t size)
{
    return _heap_pvPortCalloc(count, size, NULL, 0, __builtin_return_address(0));
}

void* IRAM_ATTR realloc(void* ptr, size_t size)
{
    return _heap_pvPortRealloc(ptr, size, NULL, 0, __builtin_return_address(0));
}

void IRAM_ATTR free(void* ptr)
{
    _heap_vPortFree(ptr, NULL, 0, __builtin_return_address(0));
}

#else
///////////////////////////////////////////////////////////////////////////////
// Non-debug path
//
// Make Non-debug Portable Heap wrappers ultra thin with STATIC_ALWAYS_INLINE
#define STATIC_ALWAYS_INLINE static ALWAYS_INLINE

STATIC_ALWAYS_INLINE
void* IRAM_ATTR _heap_pvPortMalloc(size_t size, const char* file, int line, const void *caller)
{
    (void)file;
    (void)line;
    (void)caller;
    return UMM_MALLOC(size);
}

STATIC_ALWAYS_INLINE
void* IRAM_ATTR _heap_pvPortCalloc(size_t count, size_t size, const char* file, int line, const void *caller)
{
    (void)file;
    (void)line;
    (void)caller;
    return UMM_CALLOC(count, size);
}

STATIC_ALWAYS_INLINE
void* IRAM_ATTR _heap_pvPortRealloc(void *ptr, size_t size, const char* file, int line, const void *caller)
{
    (void)file;
    (void)line;
    (void)caller;
    return UMM_REALLOC(ptr, size);
}

STATIC_ALWAYS_INLINE
void* IRAM_ATTR _heap_pvPortZalloc(size_t size, const char* file, int line, const void *caller)
{
    (void)file;
    (void)line;
    (void)caller;
    return UMM_ZALLOC(size);
}

STATIC_ALWAYS_INLINE
void IRAM_ATTR _heap_vPortFree(void *ptr, const char* file, int line, const void *caller)
{
    (void)file;
    (void)line;
    (void)caller;
    UMM_FREE(ptr);
}
#endif

/*
  NONOS SDK and lwIP expect DRAM memory. Ensure they don't get something else
  like an IRAM Heap allocation. Since they also use portable malloc calls
  pvPortMalloc, ... we can leverage that for this solution.
  Force pvPortMalloc, ... APIs to serve DRAM only.

  _heap_xxx() functions will be inline for non-debug builds.

  capture caller return address at exposed API
*/
void* IRAM_ATTR pvPortMalloc(size_t size, const char* file, int line)
{
    HeapSelectDram ephemeral;
    return _heap_pvPortMalloc(size,  file, line, __builtin_return_address(0));
}

void* IRAM_ATTR pvPortCalloc(size_t count, size_t size, const char* file, int line)
{
    HeapSelectDram ephemeral;
    return _heap_pvPortCalloc(count, size,  file, line, __builtin_return_address(0));
}

void* IRAM_ATTR pvPortRealloc(void *ptr, size_t size, const char* file, int line)
{
    HeapSelectDram ephemeral;
    return _heap_pvPortRealloc(ptr, size,  file, line, __builtin_return_address(0));
}

void* IRAM_ATTR pvPortZalloc(size_t size, const char* file, int line)
{
    HeapSelectDram ephemeral;
    return _heap_pvPortZalloc(size, file, line, __builtin_return_address(0));
}

void IRAM_ATTR vPortFree(void *ptr, const char* file, int line)
{
#if defined(UMM_POISON_CHECK) || defined(UMM_INTEGRITY_CHECK)
    // While umm_free internally determines the correct heap, UMM_POISON_CHECK
    // and UMM_INTEGRITY_CHECK do not have arguments. They will use the
    // current heap to identify which one to analyze.
    //
    // This is not needed for UMM_POISON_CHECK_LITE, it directly handles
    // multiple heaps.
    //
    // DEBUG_ESP_OOM is not tied to any one heap.
    HeapSelectDram ephemeral;
#endif
    return _heap_vPortFree(ptr, file, line, __builtin_return_address(0));
}

///////////////////////////////////////////////////////////////////////////////
// NONOS SDK - Replacement functions
//
size_t IRAM_ATTR xPortWantedSizeAlign(size_t size)
{
    return (size + 3) & ~((size_t) 3);
}

void system_show_malloc(void)
{
#ifdef UMM_INFO
    HeapSelectDram ephemeral;
    umm_info(NULL, true);
#endif
}

};

#if defined(ENABLE_THICK_DEBUG_WRAPPERS) && defined(EXPERIMENTAL_DELETE_OPERATOR)
///////////////////////////////////////////////////////////////////////////////
//C Note I just threw this together from files from the Internet
//C Is this the proper way to supply replacement delete operator?
//
#include <bits/c++config.h>

#if !_GLIBCXX_HOSTED
// A freestanding C runtime may not provide "free" -- but there is no
// other reasonable way to implement "operator delete".
namespace std
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION
  extern "C" void free(void*);
_GLIBCXX_END_NAMESPACE_VERSION
} // namespace
#pragma message("!_GLIBCXX_HOSTED")
#else
// #pragma message("_GLIBCXX_HOSTED")
// This is the path taken
#include <cstdlib>
#endif

#include "new"

// The sized deletes are defined in other files.
#pragma GCC diagnostic ignored "-Wsized-deallocation"

// These function replace their weak counterparts tagged with _GLIBCXX_WEAK_DEFINITION
void operator delete(void* ptr) noexcept
{
    _heap_vPortFree(ptr, NULL, 0, __builtin_return_address(0));
}

void operator delete(void* ptr, std::size_t) noexcept
{
    _heap_vPortFree(ptr, NULL, 0, __builtin_return_address(0));
}
#endif

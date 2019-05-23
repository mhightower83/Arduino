/*
 core_esp8266_nested_lock.h - counted interrupt disable and reenable on zero count

 Copyright (c) 2019 Michael Hightgower. All rights reserved.
 This file is part of the esp8266 core for Arduino environment.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _CORE_ESP8266_NESTED_LOCK
#define _CORE_ESP8266_NESTED_LOCK

#ifndef xt_rsr_ps
#define xt_rsr_ps()  (__extension__({uint32_t state; __asm__ __volatile__("rsr.ps %0; esync" : "=a" (state)); state;}))
inline unsigned char get_xt_intlevel(void) { return (unsigned char)xt_rsr_ps() & (unsigned char)0x0FU; }
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <c_types.h>

//#define DEBUG_NESTED_LOCK_INFO 1
//#define UMM_CRITICAL_METHOD 1
#define WRAP_ETS_INTR_LOCK 1
 /*
  * There are three methods for handling for UMM_CRITICAL_ENTRY/EXIT
  *   UMM_CRITICAL_METHOD undefined - uses the original ets_intr_lock/unlock
  *   -DUMM_CRITICAL_METHOD=1 - Deluxe nested lock
  *   -DUMM_CRITICAL_METHOD=2 - simple nested lock
  *
  * -DDEBUG_NESTED_LOCK_INFO=1
  * Will track MAX INTLEVEL at entry to lock function and MAX depth of nesting.
  * Not implimented at this time for -DUMM_CRITICAL_METHOD=2.
  *
  * -DWRAP_ETS_INTR_LOCK=1
  * Uses a wrapper to divert all calls to the defined UMM_CRITICAL_METHOD.
  * Requires linker options:
  *   -Wl,-wrap,ets_intr_lock -Wl,-wrap,ets_intr_unlock
  * For platform.txt find "compiler.c.elf.flags=..." and append:
  * "-Wl,-wrap,ets_intr_lock -Wl,-wrap,ets_intr_unlock" to the end of the line.
  *
  * If combined with DEBUG_NESTED_LOCK_INFO=1 and the UMM_CRITICAL_METHOD is
  * left undefined, you can get the the information that is being tracked on
  * the calls for the SDK's ets_intr_lock/unlock function.
  *
  */

#if DEBUG_NESTED_LOCK_INFO
size_t ICACHE_RAM_ATTR get_nested_lock_depth(void);
size_t ICACHE_RAM_ATTR get_nested_lock_depth_max(void);
unsigned char ICACHE_RAM_ATTR get_nested_lock_intlevel_max(void);
size_t ICACHE_RAM_ATTR get_nested_lock_max_elapse_time_us(void);
int ICACHE_RAM_ATTR set_nested_lock_dbg_print(int level);
void ICACHE_RAM_ATTR nested_lock_info_reset(void);
void ICACHE_RAM_ATTR nested_lock_info_print_report(void);
#else
#define get_nested_lock_depth() (__extension__({ (size_t)0U; }))
#define get_nested_lock_depth_max() (__extension__({ (size_t)0U; }))
#define get_nested_lock_intlevel_max() (__extension__({ (unsigned char)0U; }))
#define get_nested_lock_max_elapse_time_us() (__extension__({ (unsigned char)0U; }))
#define set_nested_lock_dbg_print(a) (__extension__({ (unsigned char)0U; }))
#define nested_lock_info_print_report() (__extension__({ (unsigned char)0U; }))
#endif

#if UMM_CRITICAL_METHOD == 1
/*
 * -DDEBUG_DLX_NESTED_LOCK=1
 * Call panic() when there are more calls to exit than entry.
 *
 * -DDEBUG_DLX_NESTED_LOCK=2
 * In addition to above, call panic() when the the saved_ps[] size has been
 * exceeded.
 */
// #ifndef DEBUG_DLX_NESTED_LOCK
//   #define DEBUG_DLX_NESTED_LOCK 1 // 2
// #endif
#endif

#if (UMM_CRITICAL_METHOD == 1) || (UMM_CRITICAL_METHOD == 2)

#define NESTED_LOCK_INTLEVEL                  3
#define MAX_INTLEVEL                         15

void ICACHE_RAM_ATTR nested_lock_entry(void);
void ICACHE_RAM_ATTR nested_lock_exit(void);

#else // #if (UMM_CRITICAL_METHOD == 1) || (UMM_CRITICAL_METHOD == 2)

// Use the old SDK ets_intr_lock/ets_intr_unlock
#define nested_lock_entry ets_intr_lock
#define nested_lock_exit ets_intr_unlock

#endif // #if (UMM_CRITICAL_METHOD == 1) || (UMM_CRITICAL_METHOD == 2)

#if WRAP_ETS_INTR_LOCK
void ICACHE_RAM_ATTR __wrap_ets_intr_lock(void);
void ICACHE_RAM_ATTR __wrap_ets_intr_unlock(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // #ifndef _CORE_ESP8266_NESTED_LOCK

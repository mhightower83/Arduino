/*
core_esp8266_nested_lock.cpp - counted interrupt disable and reenable on zero count

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

#include "Arduino.h"
#include "Esp.h"
#include "core_esp8266_nested_lock.h"


#ifndef MAX_SIZET
#define MAX_SIZET (~(size_t)0)
#endif

#ifndef xt_rsr_ps
#define xt_rsr_ps()  (__extension__({uint32_t state; __asm__ __volatile__("rsr.ps %0;" : "=a" (state)); state;}))
#endif

extern "C" {
int postmortem_printf(const char *str, ...) __attribute__((format(printf, 1, 2)));
void postmortem_inflight_stack_dump(uint32_t ps);
};

#define DEBUG_MSG_PRINTF_PM(fmt, ...) postmortem_printf( PSTR(fmt), ##__VA_ARGS__)
#define DEBUG_LOG_LOCATION() DEBUG_MSG_PRINTF_PM("Debug Log Location: %S:%d %S", PSTR(__FILE__), __LINE__, __func__)
#define ASSERT(exp) if (!(exp)) { DEBUG_LOG_LOCATION(); DEBUG_MSG_PRINTF_PM("\nAssert failed: %S\n", PSTR(#exp)); }

// #include <assert.h>

// #if !defined(DEBUG_ESP_PORT)
// #undef ASSERT
// #define ASSERT(...) do {} while(false)
// #elif defined(DEBUG_ASSERT_FAIL)
// #undef ASSERT
// #define ASSERT assert
// #endif

extern "C" {

#if UMM_CRITICAL_METHOD == 1
 /*
  * A nested counted lock -
  *
  * On each nested_lock_entry() call a depth count is incremented and on each
  * nested_lock_exit() that depth count is decremented.
  *
  * At each entry the current calling INTLEVEL is compared to the default for
  * nested lock, NESTED_LOCK_INTLEVEL. The higher value is keep and the
  * previous is saved, up to a limit defined by NESTED_LOCK_SAVEDPS_LIMIT.
  *
  * When NESTED_LOCK_SAVEDPS_LIMIT is exceeded the INTLEVEL is kept at its
  * peak value until nested lock unwinds down to the NESTED_LOCK_SAVEDPS_LIMIT.
  * Then each nested_lock_exit will restore the orginal INTLEVEL for that
  * matching nested_lock_entry.
  *
  */
constexpr size_t NESTED_LOCK_SAVEDPS_LIMIT =  8U;  // two is the minimum we can get away with

static struct _DLX_NESTED_LOCK {
    uint32_t saved_ps[NESTED_LOCK_SAVEDPS_LIMIT];
    size_t depth;
#if DEBUG_NESTED_LOCK_INFO
    size_t max_depth;
    uint32_t start_cycle_count;
    uint32_t max_elapse_cycle_count;
    unsigned char max_intlevel;
} _nested_lock_dx = { {0U}, 0U, 0U, 0U, 0U, 0U };
#else
} _nested_lock_dx = { {0U}, 0U };
#endif

#if DEBUG_NESTED_LOCK_INFO

#if !defined(MAX_INT_DISABLED_TIME_US)
#define MAX_INT_DISABLED_TIME_US 10
#endif

#define MAX_NESTED_LOCK_DEPTH (15)
#if !defined(MAX_NESTED_LOCK_DEPTH)
#define MAX_NESTED_LOCK_DEPTH  NESTED_LOCK_SAVEDPS_LIMIT
#endif

#define MAX_INT_DISABLED_CYCLE_COUNT \
           ( MAX_INT_DISABLED_TIME_US * clockCyclesPerMicrosecond() )

size_t get_nested_lock_depth(void) {
    return _nested_lock_dx.depth;
}

size_t get_nested_lock_depth_max(void) {
    return _nested_lock_dx.max_depth;
}

unsigned char get_nested_lock_intlevel_max(void) {
    return _nested_lock_dx.max_intlevel;
}

size_t get_nested_lock_max_elapse_cycle_count(void) {
    return _nested_lock_dx.max_elapse_cycle_count;
}

size_t get_nested_lock_max_elapse_time_us(void) {
    return _nested_lock_dx.max_elapse_cycle_count/clockCyclesPerMicrosecond();
}

inline void printReport(void) {
  DEBUG_MSG_PRINTF_PM("MAX Nested lock depth: %u\n", get_nested_lock_depth_max());
  DEBUG_MSG_PRINTF_PM("MAX Nested lock held time: %u us\n", get_nested_lock_max_elapse_time_us());
  DEBUG_MSG_PRINTF_PM("MAX Nested lock INTLEVEL:  %u\n", get_nested_lock_intlevel_max());
  DEBUG_MSG_PRINTF_PM("Current INTLEVEL: %u\n", xt_rsr_ps() & 0x0FU);
}
//D inline uint32_t getCycleCount() { // Copied from Esp.h
//D     uint32_t ccount;
//D     __asm__ __volatile__("esync; rsr %0,ccount":"=a" (ccount));
//D     return ccount;
//D }
#endif

#if !defined(MAX_NESTED_LOCK_DEPTH)
#define MAX_NESTED_LOCK_DEPTH  (NESTED_LOCK_SAVEDPS_LIMIT * 2)
#endif

void nested_lock_entry(void) {
    uint32_t savedPS = xt_rsr_ps();
    unsigned char intLevel = (unsigned char)0x0F & (unsigned char)savedPS;
    if (NESTED_LOCK_INTLEVEL > intLevel)
        xt_rsil(NESTED_LOCK_INTLEVEL);

#if !defined(DEBUG_NESTED_LOCK_INFO) || DEBUG_NESTED_LOCK_INFO == 0
    size_t depth = _nested_lock_dx.depth;
    if (NESTED_LOCK_SAVEDPS_LIMIT > depth) {
        _nested_lock_dx.saved_ps[depth] = savedPS;
        // If we have run out of room. Stop storing PS and on the Exit side
        // don't take values off until we pass below NESTED_LOCK_SAVEDPS_LIMIT.
        // This logic should make the event harmless. INTLEVEL will remain set
        // to a higher level until the nested calls unwind down to the
        // number we have for storing PS. Nobody should stay in a critial
        // state that long anyway.
    }
    depth += 1;
    ASSERT(MAX_NESTED_LOCK_DEPTH > depth);
    _nested_lock_dx.depth = depth;
#else
    if (intLevel > _nested_lock_dx.max_intlevel)
        _nested_lock_dx.max_intlevel = intLevel;

    size_t depth = _nested_lock_dx.depth;
    if (0 == depth)
        _nested_lock_dx.start_cycle_count = ESP.getCycleCount();

    if (NESTED_LOCK_SAVEDPS_LIMIT > depth) {
        _nested_lock_dx.saved_ps[depth] = savedPS;
    }
    depth += 1;
    _nested_lock_dx.depth = depth;

    if (depth > _nested_lock_dx.max_depth)
        _nested_lock_dx.max_depth = depth;

    ASSERT(MAX_NESTED_LOCK_DEPTH > depth);
#endif
}

#if DEBUG_NESTED_LOCK_INFO
int nested_lock_dbg_print_level = 0;
int set_nested_lock_dbg_print(int level) {
    int previous = nested_lock_dbg_print_level;
    nested_lock_dbg_print_level = level;
    return previous;
}

void nested_lock_info_reset(void) {
  _nested_lock_dx.max_depth = 0U;
  _nested_lock_dx.max_intlevel = 0U;
  _nested_lock_dx.max_elapse_cycle_count = 0U;

}
#endif

void nested_lock_exit(void) {
    _nested_lock_dx.depth -= 1U;
    size_t depth = _nested_lock_dx.depth;

    if (MAX_SIZET == depth) {
        ASSERT(MAX_SIZET != depth);
        // Too many exit calls(), Attempt recovery by Clamping value
        _nested_lock_dx.depth = 0U;
        return;
    }

    if (NESTED_LOCK_SAVEDPS_LIMIT > depth) {
#if DEBUG_NESTED_LOCK_INFO
        if (0 == depth) {
            uint32_t elapse_cycle_count = ESP.getCycleCount() - _nested_lock_dx.start_cycle_count;
            if (elapse_cycle_count > _nested_lock_dx.max_elapse_cycle_count) {
                _nested_lock_dx.max_elapse_cycle_count = elapse_cycle_count;
                if (1 <= nested_lock_dbg_print_level &&
                    elapse_cycle_count >= MAX_INT_DISABLED_CYCLE_COUNT) {
                    printReport();
                    DEBUG_MSG_PRINTF_PM("Current Nested lock time: %u us > %u us.\n",
                        elapse_cycle_count/clockCyclesPerMicrosecond(),
                        MAX_INT_DISABLED_CYCLE_COUNT/clockCyclesPerMicrosecond());
                    ASSERT(elapse_cycle_count < MAX_INT_DISABLED_CYCLE_COUNT);
                }
                if (2 <= nested_lock_dbg_print_level &&
                    elapse_cycle_count >= (MAX_INT_DISABLED_CYCLE_COUNT * 2)) {
                    postmortem_inflight_stack_dump(_nested_lock_dx.saved_ps[depth]);
                    // do_once = false;
                }
            }
        }
#endif
        xt_wsr_ps(_nested_lock_dx.saved_ps[depth]);
    }
}

#elif UMM_CRITICAL_METHOD == 2
 /*
  * A simple nested counted lock - to implimented interrupt disable and
  * restore, when count returns to zero.
  *
  * Weakness: w/o before/after INTLEVEL monitoring, INTLEVEL may be
  * inadvertently lowered.
  *
  */
static struct _SIMPLE_NESTED_LOCK  {
    uint32_t saved_ps;
    size_t depth;
} _nested_lock = { 0U, 0U };

#if DEBUG_NESTED_LOCK_INFO
size_t get_nested_lock_depth(void) { return _nested_lock.depth;}
// Stubs - not implemented yet.
size_t get_nested_lock_depth_max(void) { return 0U;}
unsigned char get_nested_lock_intlevel_max(void) { return 0U;}
#endif

void nested_lock_entry(void) {
    // INTLEVEL 3 is the same value that ets_intr_lock() uses.
    uint32_t savePS = xt_rsil(NESTED_LOCK_INTLEVEL);
    if (0U == _nested_lock.depth){
      _nested_lock.saved_ps = savePS;
    }
    _nested_lock.depth++;
}

void nested_lock_exit(void) {
    _nested_lock.depth--;
    if (0U == _nested_lock.depth) {
        xt_wsr_ps(_nested_lock.saved_ps);
    }
}
#endif

#if WRAP_ETS_INTR_LOCK
#if (UMM_CRITICAL_METHOD == 1) || (UMM_CRITICAL_METHOD == 2)

void ICACHE_RAM_ATTR __wrap_ets_intr_lock(void) {
    nested_lock_entry();
}

void ICACHE_RAM_ATTR __wrap_ets_intr_unlock(void){
    nested_lock_exit();
}
#else // #if (UMM_CRITICAL_METHOD == 1) || (UMM_CRITICAL_METHOD == 2)

// passthrough to the old ets_intr_lock ...

extern void __real_ets_intr_lock(void);
extern void __real_ets_intr_unlock(void);

#if DEBUG_NESTED_LOCK_INFO
static struct _TRACK_ESP_INTR_LOCK {
    size_t depth;
    size_t max_depth;
    unsigned char max_intlevel;
} esp_lock_stats = { 0U, 0U, 0U };

size_t get_nested_lock_depth(void) { return esp_lock_stats.depth;}
size_t get_nested_lock_depth_max(void) { return esp_lock_stats.max_depth;}
unsigned char get_nested_lock_intlevel_max(void) { return esp_lock_stats.max_intlevel;}
#endif

/*
  This is what the SDK is doing

00000f74 <ets_intr_lock>:
    f74:   006320      rsil    a2, 3
    f77:   fffe31      l32r    a3, f70
    f7a:   0329        s32i.n  a2, a3, 0
    f7c:   f00d        ret.n

00000f80 <ets_intr_unlock>:
    f80:   006020      rsil    a2, 0
    f83:   f00d        ret.n

*/

void ICACHE_RAM_ATTR __wrap_ets_intr_lock(void) {
#if DEBUG_NESTED_LOCK_INFO
    uint32_t savedPS = xt_rsr_ps();
#endif
    __real_ets_intr_lock();

#if DEBUG_NESTED_LOCK_INFO
    unsigned char intLevel = (unsigned char)0x0FU & (unsigned char)savedPS;
    if (intLevel > esp_lock_stats.max_intlevel)
        esp_lock_stats.max_intlevel = intLevel;

    esp_lock_stats.depth += 1U;
    size_t depth = esp_lock_stats.depth;
    if (depth > esp_lock_stats.max_depth)
        esp_lock_stats.max_depth = depth;
#endif // #if DEBUG_NESTED_LOCK_INFO
}

void ICACHE_RAM_ATTR __wrap_ets_intr_unlock(void){
#if DEBUG_NESTED_LOCK_INFO
    esp_lock_stats.depth -= 1U;
#endif
    __real_ets_intr_unlock();
}
#endif  // #if (UMM_CRITICAL_METHOD == 1) || (UMM_CRITICAL_METHOD == 2)
#endif  // #if WRAP_ETS_INTR_LOCK

}; // extern "C" {

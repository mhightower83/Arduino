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
#if 0
#include <assert.h>
#else
#define assert(...) do {} while(false)
#endif
//D
//D // #include <interrupts.h>
//D #ifndef __STRINGIFY
//D #define __STRINGIFY(a) #a
//D #endif
//D #ifndef xt_rsil
//D #define xt_rsil(level) (__extension__({uint32_t state; __asm__ __volatile__("rsil %0," __STRINGIFY(level) : "=a" (state)); state;}))
//D #endif
//D #ifndef xt_wsr_ps
//D #define xt_wsr_ps(state)  __asm__ __volatile__("wsr %0,ps; isync" :: "a" (state) : "memory")
//D #endif
#ifndef xt_rsr_ps
#define xt_rsr_ps()  (__extension__({uint32_t state; __asm__ __volatile__("rsr.ps %0;" : "=a" (state)); state;}))
#endif

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
#if 0
    uint32_t savedPS = xt_rsr_ps();
    unsigned char intLevel = (unsigned char)0x0F & (unsigned char)savedPS;
    if (NESTED_LOCK_INTLEVEL > intLevel)
        xt_rsil(NESTED_LOCK_INTLEVEL);
#else
    uint32_t savedPS = xt_rsil(MAX_INTLEVEL);
    unsigned char intLevel = (unsigned char)0x0F & (unsigned char)savedPS;
    if (NESTED_LOCK_INTLEVEL > intLevel) {
        xt_rsil(NESTED_LOCK_INTLEVEL);
    } else {
        xt_wsr_ps(savedPS);
    }
#endif

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
    assert(MAX_NESTED_LOCK_DEPTH > depth);

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

    assert(MAX_NESTED_LOCK_DEPTH > depth);
#endif
}

void nested_lock_exit(void) {
    _nested_lock_dx.depth -= 1U;
    size_t depth = _nested_lock_dx.depth;
#if 1 //DEBUG_NESTED_LOCK_INFO == 0
    if (~(size_t)0 == depth) {
        // Too many exit calls(), Attempt recovery by Clamping value
        _nested_lock_dx.depth = 0U;
        return;
    }

    if (NESTED_LOCK_SAVEDPS_LIMIT > depth)
        xt_wsr_ps(_nested_lock_dx.saved_ps[depth]);
#else
    assert((~(size_t)0) != depth);

    if (NESTED_LOCK_SAVEDPS_LIMIT > depth) {
        xt_wsr_ps(_nested_lock_dx.saved_ps[depth]);
        if (0 == depth) {
            uint32_t elapse_cycle_count = ESP.getCycleCount() - _nested_lock_dx.start_cycle_count;
            if (elapse_cycle_count > _nested_lock_dx.max_elapse_cycle_count) {
                _nested_lock_dx.max_elapse_cycle_count = elapse_cycle_count;
                assert(elapse_cycle_count < MAX_INT_DISABLED_CYCLE_COUNT);
            }
        }
    }
#endif
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

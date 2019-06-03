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

extern "C" {
int postmortem_printf(const char *str, ...) __attribute__((format(printf, 1, 2)));
// void inflight_stack_trace(uint32_t ps);
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

#if   UMM_CRITICAL_METHOD == 1
#define NESTED_LOCK_SAVEDPS_LIMIT  4
#if 0 //DEBUG_NESTED_LOCK_INFO
#define MAX_NESTED_LOCK_DEPTH  NESTED_LOCK_SAVEDPS_LIMIT
#else
#define MAX_NESTED_LOCK_DEPTH (15)
#endif

#elif UMM_CRITICAL_METHOD == 2
#define NESTED_LOCK_SAVEDPS_LIMIT  1
#define MAX_NESTED_LOCK_DEPTH (15)

#else
#define MAX_NESTED_LOCK_DEPTH (15)
#endif



#if (UMM_CRITICAL_METHOD == 1) || (UMM_CRITICAL_METHOD == 2)

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
  * It turns out that this is not a sutable replacement for ets_intr_lock.
  * The depth count goes negative when used in place of ets_intr_lock/unlock.
  *
  */
static volatile struct _DLX_NESTED_LOCK {
    uint32_t saved_ps[NESTED_LOCK_SAVEDPS_LIMIT];
    int depth;
#if DEBUG_NESTED_LOCK_INFO
    int max_depth;
    uint32_t depth_below_0_count;
    uint32_t start_nested_cycle_count;
    uint32_t max_nested_cycle_count;
    unsigned char max_intlevel;
} _lock_info = { {0U}, 0, 0, 0U, 0U, 0U, 0U };
#else
} _lock_info = { {0U}, 0 };
#endif

void nested_lock_entry(void) {
    uint32_t savedPS = xt_rsr_ps();
    unsigned char intLevel = (unsigned char)0x0F & (unsigned char)savedPS;
    if (NESTED_LOCK_INTLEVEL > intLevel)
        xt_rsil(NESTED_LOCK_INTLEVEL);

#if !defined(DEBUG_NESTED_LOCK_INFO) || DEBUG_NESTED_LOCK_INFO == 0
    int depth = _lock_info.depth;

    /*
    This is broken when depth is negative, need to rethink how to handle saving PS.
    Need to make sure we have a valid INTLEVEL 0 to restore?
    The net calls to lock to unlock are balanced. They just go negative?

    Adjustments made - not tested
    */

    if (NESTED_LOCK_SAVEDPS_LIMIT > depth && 0 <= depth) {
        _lock_info.saved_ps[depth] = savedPS;
        // If we have run out of room. Stop storing PS and on the Exit side
        // don't take values off until we pass below NESTED_LOCK_SAVEDPS_LIMIT.
        // This logic should make the event harmless. INTLEVEL will remain set
        // to a higher level until the nested calls unwind down to the
        // number we have for storing PS. Nobody should stay in a critial
        // state that long anyway.
    }

    depth += 1;
    ASSERT(MAX_NESTED_LOCK_DEPTH > depth);
    _lock_info.depth = depth;
#else
    if (intLevel > _lock_info.max_intlevel)
        _lock_info.max_intlevel = intLevel;

    size_t depth = _lock_info.depth;
    if (0 == depth)
        _lock_info.start_nested_cycle_count = ESP.getCycleCount();

    if (NESTED_LOCK_SAVEDPS_LIMIT > depth) {
        _lock_info.saved_ps[depth] = savedPS;
    }
    depth += 1;
    _lock_info.depth = depth;

    if (depth > _lock_info.max_depth)
        _lock_info.max_depth = depth;

    ASSERT(MAX_NESTED_LOCK_DEPTH > depth);
#endif
}

void nested_lock_exit(void) {
    // _lock_info.depth must NOT be decremented until the end
    // just in case debug prints cause us to be reentered and they do!
    int depth = _lock_info.depth - 1;

#if DEBUG_NESTED_LOCK_INFO
    if (0 == depth) {
        uint32_t elapse_cycle_count = ESP.getCycleCount() - _lock_info.start_nested_cycle_count;
        if (elapse_cycle_count > _lock_info.max_nested_cycle_count) {
            _lock_info.max_nested_cycle_count = elapse_cycle_count;
            // if (1 <= nested_lock_dbg_print_level &&
            //     elapse_cycle_count >= MAX_INT_DISABLED_CYCLE_COUNT) {
            //     nested_lock_info_print_report();
            //     DEBUG_MSG_PRINTF_PM("Current Nested lock time: %u us > %u us.\n",
            //         elapse_cycle_count/clockCyclesPerMicrosecond(),
            //         MAX_INT_DISABLED_CYCLE_COUNT/clockCyclesPerMicrosecond());
            //     ASSERT(elapse_cycle_count < MAX_INT_DISABLED_CYCLE_COUNT);
            // }
            // if (2 <= nested_lock_dbg_print_level &&
            //     elapse_cycle_count >= (MAX_INT_DISABLED_CYCLE_COUNT)) {
            //     inflight_stack_trace(_lock_info.saved_ps[depth]);
            // }
        }
    } else if (0 > depth) {
        ASSERT(0 > depth);
        panic();
    }
#endif
    _lock_info.depth = depth;
    if (NESTED_LOCK_SAVEDPS_LIMIT > depth && 0 <= depth) {
        xt_wsr_ps(_lock_info.saved_ps[depth]);
    }
}
#endif

#if defined(DEBUG_NESTED_LOCK_INFO) && defined(WRAP_ETS_INTR_LOCK)
static struct _TRACK_ESP_INTR_LOCK {
    int depth;
    int max_depth;
    int min_depth;
    uint32_t depth_below_0_count;
    uint32_t start_cycle_count;
    uint32_t start_nested_cycle_count;
    uint32_t max_elapse_cycle_count;
    uint32_t max_nested_cycle_count;
    uint32_t untracked_intlevel_change;
    uint32_t untracked_intlevel_change2;
    uint32_t untracked_intlevel_change3;
    uint32_t intlevel_already_set_count;
    uint32_t already_unlocked_count;
    bool one_shot;
    bool trip;
    unsigned char max_intlevel;
    unsigned char locked;
    unsigned char last_intlevel;
} _lock_info = { 0, 0, 0, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, false, false, 0U, 0U, 0U };
#endif

// This is now common to ets_intr_lock tacking
#if DEBUG_NESTED_LOCK_INFO

#if !defined(MAX_INT_DISABLED_TIME_US)
#define MAX_INT_DISABLED_TIME_US 10
#endif

#define MAX_INT_DISABLED_CYCLE_COUNT \
           ( MAX_INT_DISABLED_TIME_US * clockCyclesPerMicrosecond() )

int get_nested_lock_depth(void) {
    return _lock_info.depth;
}

int get_nested_lock_depth_max(void) {
    return _lock_info.max_depth;
}

int get_nested_lock_depth_min(void) {
    return _lock_info.min_depth;
}

size_t get_lock_depth_below_0_count(void) {
    return _lock_info.depth_below_0_count;
}

unsigned char get_nested_lock_intlevel_max(void) {
    return _lock_info.max_intlevel;
}

size_t get_nested_lock_max_cycle_count(void) {
    return _lock_info.max_nested_cycle_count;
}

size_t get_nested_lock_max_time_us(void) {
    return _lock_info.max_nested_cycle_count/clockCyclesPerMicrosecond();
}

size_t get_lock_max_elapse_cycle_count(void) {
    return _lock_info.max_elapse_cycle_count;
}

size_t get_lock_max_elapse_time_us(void) {
    return _lock_info.max_elapse_cycle_count/clockCyclesPerMicrosecond();
}

uint32_t get_intlevel_already_set_count(void) {
    return _lock_info.intlevel_already_set_count;
}

uint32_t get_already_unlocked_count(void) {
  return _lock_info.already_unlocked_count;
}

uint32_t get_untracked_intlevel_change(void) {
  return _lock_info.untracked_intlevel_change;
}

uint32_t get_untracked_intlevel_change2(void) {
  return _lock_info.untracked_intlevel_change2;
}

uint32_t get_untracked_intlevel_change3(void) {
  return _lock_info.untracked_intlevel_change3;
}

// int nested_lock_dbg_print_level = 0;
// int set_nested_lock_dbg_print(int level) {
//     int previous = nested_lock_dbg_print_level;
//     nested_lock_dbg_print_level = level;
//     return previous;
// }

void nested_lock_info_reset(void) {
    _lock_info.max_depth = 0U;
    _lock_info.max_intlevel = 0U;
    _lock_info.max_elapse_cycle_count = 0U;
    _lock_info.max_nested_cycle_count = 0U;
    #if defined(WRAP_ETS_INTR_LOCK)
    _lock_info.one_shot = true;
    _lock_info.trip = false;
    #endif
}

void nested_lock_info_print_report(void) {
    DEBUG_MSG_PRINTF_PM("Current Nested lock depth:  %u\n", get_nested_lock_depth());
    DEBUG_MSG_PRINTF_PM("Nested lock depth below 0:  %u\n", get_lock_depth_below_0_count());
    DEBUG_MSG_PRINTF_PM("MIN Nested lock depth:      %u\n", get_nested_lock_depth_min());
    DEBUG_MSG_PRINTF_PM("MAX Nested lock depth:      %u\n", get_nested_lock_depth_max());
    DEBUG_MSG_PRINTF_PM("MAX Nested lock held time:  %u us\n", get_nested_lock_max_time_us());
    DEBUG_MSG_PRINTF_PM("MAX lock held time:         %u us\n", get_lock_max_elapse_time_us());
    DEBUG_MSG_PRINTF_PM("MAX Nested lock INTLEVEL:   %u\n", get_nested_lock_intlevel_max());
    DEBUG_MSG_PRINTF_PM("Current INTLEVEL:           %u\n", xt_rsr_ps() & 0x0FU);
    DEBUG_MSG_PRINTF_PM("intlevel_already_set_count: %u\n", get_intlevel_already_set_count());
    DEBUG_MSG_PRINTF_PM("already_unlocked_count:     %u\n", get_already_unlocked_count());
    DEBUG_MSG_PRINTF_PM("untracked_intlevel_change:  %u\n", get_untracked_intlevel_change());
    DEBUG_MSG_PRINTF_PM("untracked_intlevel_change2: %u\n", get_untracked_intlevel_change2());
    DEBUG_MSG_PRINTF_PM("untracked_intlevel_change3: %u\n", get_untracked_intlevel_change3());
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
#if DEBUG_NESTED_LOCK_INFO
/*
   This is a log of code to monitor the ets_intr_lock/ets_intr_unlock.
   The "Heisenberg uncertainty principle" or perhaps more acuratly the
   "Observer Effect" should be keeped in mind when looking at the results.
 */

void ICACHE_RAM_ATTR __wrap_ets_intr_lock(void) {

    uint32_t savedPS = xt_rsr_ps();
    unsigned char intLevel = (unsigned char)0x0F & (unsigned char)savedPS;

    __real_ets_intr_lock();

    int depth = _lock_info.depth;

    // This one occurs, but not often.
    // This can occur if unlock one more time than lock.
    // Then on the 2nd call to lock this will be true.
    //
    // Maybe add code to see how long the depth stays negative
    // and do a stack trace on event.
    if (0 == depth &&       0U <  _lock_info.locked)
        _lock_info.untracked_intlevel_change  += 1U;

    // Haven't seen any of these
    if (1 <= depth && intLevel != _lock_info.locked)
        _lock_info.untracked_intlevel_change2 += 1U;

    // Haven't seen any of these
    if (1 <= depth && intLevel >  _lock_info.locked)
        _lock_info.untracked_intlevel_change3 += 1U;

    // Haven't seen any of these
    if (0 == depth && 0U < intLevel)
        _lock_info.intlevel_already_set_count += 1U;

    _lock_info.locked = 3;
    if (intLevel > _lock_info.max_intlevel)
        _lock_info.max_intlevel = intLevel;

    _lock_info.last_intlevel = intLevel;

    if (0U == intLevel)
        _lock_info.start_cycle_count = ESP.getCycleCount();

    if (0 == depth)
        _lock_info.start_nested_cycle_count = ESP.getCycleCount();

    depth += 1;
    _lock_info.depth = depth;

    if (depth > _lock_info.max_depth)
        _lock_info.max_depth = depth;

    // if (_lock_info.one_shot && depth == 2) {
    //     inflight_stack_trace(xt_rsr_ps());
    //     _lock_info.one_shot = false;
    //     _lock_info.trip = true;
    // }
    //
    // if (1 <= nested_lock_dbg_print_level)
    //     ASSERT(MAX_NESTED_LOCK_DEPTH > depth);
}

void ICACHE_RAM_ATTR __wrap_ets_intr_unlock(void){
    // _lock_info.depth must NOT be decremented until the end
    // just in case debug prints cause us to be reentered and they do!
    int depth = _lock_info.depth - 1;

    uint32_t now_in_cycles = ESP.getCycleCount();
    // if (_lock_info.trip) { // && depth >= 0U) {
    //     uint32_t elapsed = now_in_cycles - _lock_info.start_cycle_count;
    //     inflight_stack_trace(xt_rsr_ps());
    //     DEBUG_MSG_PRINTF_PM("Last lock to first unlock time: %u us > %u us.\n",
    //         elapsed/clockCyclesPerMicrosecond(),
    //         MAX_INT_DISABLED_CYCLE_COUNT/clockCyclesPerMicrosecond());
    //     if (0 == depth)
    //         _lock_info.trip = false;
    // }

    if (0 == depth) {
        uint32_t elapsed = now_in_cycles - _lock_info.start_nested_cycle_count;
        if (elapsed > _lock_info.max_nested_cycle_count)
            _lock_info.max_nested_cycle_count = elapsed;
    } if (0 > depth) {
       _lock_info.depth_below_0_count += 1;
    }

    if (depth < _lock_info.min_depth)
        _lock_info.min_depth = depth;

    if (0U == _lock_info.locked) {
        _lock_info.already_unlocked_count += 1U;
    } else {
        uint32_t elapsed = now_in_cycles - _lock_info.start_cycle_count;
        if (elapsed > _lock_info.max_elapse_cycle_count) {
            _lock_info.max_elapse_cycle_count = elapsed;
            // if (1 <= nested_lock_dbg_print_level &&
            //     elapsed >= MAX_INT_DISABLED_CYCLE_COUNT) {
            //     nested_lock_info_print_report();
            //     DEBUG_MSG_PRINTF_PM("Current Nested lock time: %u us > %u us.\n",
            //         elapsed/clockCyclesPerMicrosecond(),
            //         MAX_INT_DISABLED_CYCLE_COUNT/clockCyclesPerMicrosecond());
            //     ASSERT(elapsed < MAX_INT_DISABLED_CYCLE_COUNT);
            // }
            // if (2 <= nested_lock_dbg_print_level &&
            //     elapsed >= (MAX_INT_DISABLED_CYCLE_COUNT)) {
            //     inflight_stack_trace(0);
            // }
        }
    }
    _lock_info.depth = depth;
    _lock_info.locked = 0U;
    __real_ets_intr_unlock();
}
#else // #if DEBUG_NESTED_LOCK_INFO
void ICACHE_RAM_ATTR __wrap_ets_intr_lock(void) {
    __real_ets_intr_lock();
}

void ICACHE_RAM_ATTR __wrap_ets_intr_unlock(void){
__real_ets_intr_unlock();
}
#endif  // #if DEBUG_NESTED_LOCK_INFO
#endif  // #if (UMM_CRITICAL_METHOD == 1) || (UMM_CRITICAL_METHOD == 2)
#endif  // #if WRAP_ETS_INTR_LOCK

}; // extern "C" {

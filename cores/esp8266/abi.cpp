/*
 Copyright (c) 2014 Arduino.  All right reserved.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <assert.h>
#include <debug.h>
#include <Arduino.h>
#include <cxxabi.h>

using __cxxabiv1::__guard;

// Debugging helper, last allocation which returned NULL
extern void *umm_last_fail_alloc_addr;
extern int umm_last_fail_alloc_size;

extern "C" void __cxa_pure_virtual(void) __attribute__ ((__noreturn__));
extern "C" void __cxa_deleted_virtual(void) __attribute__ ((__noreturn__));

void __cxa_pure_virtual(void)
{
    panic();
}

void __cxa_deleted_virtual(void)
{
    panic();
}

#if 0
typedef struct {
    uint8_t  guard;
    uint32_t ps;
} guard_t;

static_assert((sizeof(__guard) == sizeof(guard_t)), "guard_t size missmatch.");

extern "C" int __cxa_guard_acquire(__guard* pg)
{
    uint8_t ps = xt_rsil(15);
    if (reinterpret_cast<guard_t*>(pg)->guard) {
        xt_wsr_ps(ps);
        return 0;
    }
    reinterpret_cast<guard_t*>(pg)->ps = ps;
    return 1;
}

extern "C" void __cxa_guard_release(__guard* pg)
{
    reinterpret_cast<guard_t*>(pg)->guard = 1;
    xt_wsr_ps(reinterpret_cast<guard_t*>(pg)->ps);
}

extern "C" void __cxa_guard_abort(__guard* pg)
{
    xt_wsr_ps(reinterpret_cast<guard_t*>(pg)->ps);
}
#else
// Started with https://github.com/aosm/libcppabi/blob/master/src/cxa_guard.cxx
// as reference.
// Followed the flow of libstdc++-v3/libsupc++/guard.cc along the path of
//   !_GLIBCXX_USE_FUTEX !__GTHREAD_HAS_COND !__GTHREADS
// this version https://code.woboq.org/gcc/libstdc++-v3/libsupc++/guard.cc.html
//
// Another ref https://opensource.apple.com/source/libcppabi/libcppabi-14/src/cxa_guard.cxx

// This object is a use once and is provided initialized to zero.
typedef struct {
    uint8_t guard;     // initializerHasRun, guarded static var has been initialized
    uint8_t psInUse;   // init_in_progress, pending_bit, guarded static var is being initialized
    uint8_t waiting;   // some other threads are waiting until initialized??
    uint8_t padding;
    uint32_t ps;
} guard_t;

static_assert((sizeof(__guard) == sizeof(guard_t)), "guard_t size missmatch.");

extern "C" int __cxa_guard_acquire(__guard* pg)
{
    uint32_t ps = xt_rsil(15);
    if (reinterpret_cast<guard_t*>(pg)->guard) {
        xt_wsr_ps(ps);
        return 0;
    }
    if (reinterpret_cast<guard_t*>(pg)->psInUse) {
        reinterpret_cast<guard_t*>(pg)->waiting = 1;
        xt_wsr_ps(ps);
        // // init in progress - what to do - cannot throw exception
        // return 0;   // Caller would need to test guard bit to confirm completion.
        panic();    // non-OS, single threaded no way to make called wait.
    }
    reinterpret_cast<guard_t*>(pg)->psInUse = 1;
    reinterpret_cast<guard_t*>(pg)->ps = ps;
    return 1;
}

// Restore PS
extern "C" void __cxa_guard_release(__guard* pg)
{
    if (reinterpret_cast<guard_t*>(pg)->psInUse) {
        reinterpret_cast<guard_t*>(pg)->guard = 1;
        reinterpret_cast<guard_t*>(pg)->psInUse = 0;
        reinterpret_cast<guard_t*>(pg)->waiting = 0;
        xt_wsr_ps(reinterpret_cast<guard_t*>(pg)->ps);
    } else {
        panic();
    }
}

// You can abort an acquire before it is released and reuse the guard object
// However, you cannot reuse a released guard object or abort it if not aquired.
extern "C" void __cxa_guard_abort(__guard* pg)
{
    if (reinterpret_cast<guard_t*>(pg)->psInUse) {
        reinterpret_cast<guard_t*>(pg)->psInUse = 0;
        reinterpret_cast<guard_t*>(pg)->waiting = 0;
        xt_wsr_ps(reinterpret_cast<guard_t*>(pg)->ps);
    } else {
        panic();
    }
}
#endif
// TODO: rebuild windows toolchain to make this unnecessary:
void* __dso_handle;

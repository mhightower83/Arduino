/*
 *   Copyright 2020 M Hightower
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

/*
 *  Need a description of what we are doing what this is for.
 */

 /*
 * * We as using this to get started.
 *
 *  This is the original app_entry() not providing extra 4K heap, but allowing
 *  the use of WPS.
 *
 *  see comments in core_esp8266_main.cpp's app_entry()
 *
 */

/*
 * To improve clarity of meaning on variable names I try to use "_last" on
 * names refereing to the beginning of a stack.
 */

/* -- delete this bloated description
 *
 * start, end, top, bottom, first, last, up, down as references to larger or
 * smaller memory address values all become ambigious to me when you start using
 * them to describe stacks that start at a higher address and move down. Then
 * you start getting references to buffers and they are going in the oposite
 * direction.
 *
 * It is my intent to only use start and end to augment variables that are
 * describing normal memory directions.
 *
 * I intend to use "_first" and "_last" to augment varaibles that describe a stack
 * pointer, where first will be the highest address value and last will be the
 * lowest.
 *
 * Since I came to this after writing a lot of this code I will need to
 * update names for compliance. So beware, I also have a strong tendency to
 * get things like this confused anyway.
 *
 */

#include <c_types.h>
#include "cont.h"
#include "coredecls.h"
#include <core_esp8266_features.h>
#include <esp8266_undocumented.h>

extern "C" {
#include <user_interface.h>
extern void call_user_start();
//D extern uint32_t rtc_get_reset_reason(void);
}

/*
 * DEBUG_HWDT_NO4KEXTRA
 *
 * This option will leave more of the system stack available for stack dump. The
 * problem with the "4K extra" option is that it pushes the system stack up into
 * the ROM's BSS area which gets zeroed at the ROM reboot event created by the
 * Hardware WDT.
 *
 * Using this option has the effect of taking 4K of DRAM away from the heap
 * which gets used for "cont" stack. Leaving an extra 4K on the sys stack that
 * is clear of the ROM BSS, allowing a more complete sys stack dump.
 *
 */
#define DEBUG_HWDT_NO4KEXTRA

/*
 *  Added this block to make this module complete.
 *  Stuff for a #include "hwdt_stack_dump.h"
 *
 #include "hwdt_stack_dump.h"
 */
#ifndef HWDT_STACK_DUMP_H
#define HWDT_STACK_DUMP_H

typedef struct STACK_USAGES {
    uint32_t rom;
    uint32_t sys;
    uint32_t cont;
} STACK_USAGES_t;

extern uint32_t *g_romStack;
extern STACK_USAGES_t stack_usages;

#endif
// end - #include "hwdt_stack_dump.h"

STACK_USAGES_t stack_usages __attribute__((section(".noinit")));

#define MK_ALIGN16_SZ(a) (((a) + 0x0F) & ~0x0F)
// #define UINTPTR_T uint32_t
#define UINTPTR_T uintptr_t

/*
 * ROM_STACK_SIZE
 *
 * This is the gap we maintain between the stack used by the ROM and eboot code
 * and the sketch. (by which I refer to ESP8266 Core, NONOS SDK, and sketch as
 * one.) This gap helps preserve the contents of the SDK stack we want to
 * display.
 *
 */
#ifndef ROM_STACK_SIZE
#define ROM_STACK_SIZE (1024)
#endif

/*
 * STACK_USAGES
 *
 * Gather stack usage information on ROM and eboot combined, sys, and cont.
 */
#define STACK_USAGES

/*
 * ROM_STACK_DUMP
 *
 * Dump the stack contents of the ROM Stack area.
 */
// #define ROM_STACK_DUMP

#ifndef CONT_STACKGUARD
#define CONT_STACKGUARD 0xfeefeffe
#endif

constexpr uint32_t *dram_start      = (uint32_t *)0x3FFE8000;
constexpr uint32_t *dram_end        = (uint32_t *)0x40000000;

constexpr uint32_t *rom_stack_first = (uint32_t *)0x40000000;
constexpr uint32_t *sysStack        = (uint32_t *)0x3fffeb30;
constexpr uint32_t *sysStack_e000   = (uint32_t *)0x3fffe000;


// Map out who lives where
constexpr size_t rom_stack_A16_sz = MK_ALIGN16_SZ(ROM_STACK_SIZE);
constexpr size_t cont_stack_A16_sz = MK_ALIGN16_SZ(sizeof(cont_t));
constexpr uint32_t *romStack = (uint32_t *)((uintptr_t)rom_stack_first - rom_stack_A16_sz);


#ifdef DEBUG_HWDT_NO4KEXTRA
/* this is the default NONOS-SDK user's heap location */
static cont_t g_cont __attribute__ ((aligned (16)));
//? constexpr uint32_t *cont_stack_first = (uint32_t *)((uintptr_t));
//? constexpr cont_t *contStack = (cont_t *)&g_cont;
constexpr uint32_t *sys_stack_first = (uint32_t *)((uintptr_t)romStack);

#else
constexpr uint32_t *cont_stack_first = (uint32_t *)((uintptr_t)romStack); // only for computation
constexpr cont_t *contStack = (cont_t *)((uintptr_t)cont_stack_first - cont_stack_A16_sz);
constexpr uint32_t *sys_stack_first = (uint32_t *)((uintptr_t)contStack);
#endif


constexpr volatile uint32_t *RTC_SYS = (volatile uint32_t*)0x60001100;
/*
 *  ... need to review for what has changed
 *
 *  Another thought save romStack address as the SP to use when starting
 *  the SDK. Reset the stack pointer back to the beginning, with in the
 *  romStack array. Do all the stack dump work withing the confines of
 *  romStack. Then set SP to romStack and call call_user_start();
 *
 */

uint32_t *g_romStack  __attribute__((section(".noinit")));
size_t g_rom_stack_A16_sz  __attribute__((section(".noinit")));


void enable_debug_hwdt_at_link_time (void)
{
    /*
     * This functions does nothing; however, including a call to it in setup,
     * allows this module to override, at link time, the core_esp8266_main.cpp's
     * app_entry() with the one below. This will create a stack dump on
     * Hardware WDT resets.
     *
     * It appears just including this module in the sketch dirctory will
     * will also accomplish the same.
     *
     */
}


/* the following code is linked only if a call to the above function is made somewhere */

extern "C" {

static void ICACHE_RAM_ATTR print_size(UINTPTR_T val) {
    uint32_t fmt_sz[4];
    fmt_sz[0]  = ('0' ) | ('x' <<8) | ('%' <<16) | ('0' <<24);
    fmt_sz[1]  = ('8' ) | ('X' <<8) | (',' <<16) | (' ' <<24);
    fmt_sz[2]  = (' ' ) | ('%' <<8) | ('5' <<16) | ('u' <<24);
    fmt_sz[3]  = ('\n') | ('\0'<<8) | ('\0'<<16) | ('\0'<<24);
    ets_printf((const char *)fmt_sz, val, val);
}


enum PRINT_STACK {
    CONT = 1,
    SYS = 2,
    BODY = 4,
    CLOSE = 8,
    ROM = 16,
    PART1_CONT = (1 + 4),
    PART1_SYS = (2 + 4),
    PART1_ROM = (16 + 4),
    PART2 = (4 + 8)
} ;

static void ICACHE_RAM_ATTR print_stack(UINTPTR_T start, UINTPTR_T end, UINTPTR_T adjust, uint32_t chunk) {

    uint32_t fmt_stk[6];
    fmt_stk[0] = ('\n') | ('>' <<8) | ('>' <<16) | ('>' <<24);
    fmt_stk[1] = ('s' ) | ('t' <<8) | ('a' <<16) | ('c' <<24);
    fmt_stk[2] = ('k' ) | ('>' <<8) | ('>' <<16) | ('>' <<24);
    fmt_stk[3] = ('\n') | ('\n'<<8) | ('c' <<16) | ('t' <<24);
    fmt_stk[4] = ('x' ) | (':' <<8) | (' ' <<16) | ('%' <<24);
    fmt_stk[5] = ('s' ) | ('\n'<<8) | ('\0'<<16) | ('\0'<<24);

    uint32_t fmt_sp[9];
    fmt_sp[0]  = ('s' ) | ('p' <<8) | (':' <<16) | (' ' <<24);
    fmt_sp[1]  = ('%' ) | ('0' <<8) | ('8' <<16) | ('x' <<24);
    fmt_sp[2]  = (' ' ) | ('e' <<8) | ('n' <<16) | ('d' <<24);
    fmt_sp[3]  = (':' ) | (' ' <<8) | ('%' <<16) | ('0' <<24);
    fmt_sp[4]  = ('8' ) | ('x' <<8) | (' ' <<16) | ('o' <<24);
    fmt_sp[5]  = ('f' ) | ('f' <<8) | ('s' <<16) | ('e' <<24);
    fmt_sp[6]  = ('t' ) | (':' <<8) | (' ' <<16) | ('%' <<24);
    fmt_sp[7]  = ('0' ) | ('4' <<8) | ('x' <<16) | ('\n'<<24);
    fmt_sp[8]  = ('\0') | ('\0'<<8) | ('\0'<<16) | ('\0'<<24);

    uint32_t fmt_rom[1];
    fmt_rom[0]  = ('R' ) | ('O' <<8) | ('M' <<16) | ('\0'<<24);

    uint32_t fmt_sys[1];
    fmt_sys[0]  = ('s' ) | ('y' <<8) | ('s' <<16) | ('\0'<<24);

    uint32_t fmt_cont[2];
    fmt_cont[0] = ('c' ) | ('o' <<8) | ('n' <<16) | ('t' <<24);
    fmt_cont[1] = ('\0') | ('\0'<<8) | ('\0'<<16) | ('\0'<<24);


    if (chunk & PRINT_STACK::CONT) {
        ets_printf((const char *)fmt_stk, (const char *)fmt_cont);
    } else
    if (chunk & PRINT_STACK::SYS) {
        ets_printf((const char *)fmt_stk, (const char *)fmt_sys);
    } else
    if (chunk & PRINT_STACK::ROM) {
        ets_printf((const char *)fmt_stk, (const char *)fmt_rom);
    }

    ets_printf((const char *)fmt_sp, start + adjust, end + adjust, 0);

    {
        uint32_t fmt_stk_dmp[8];
        fmt_stk_dmp[0] = ('%') | ('0' <<8) | ('8' <<16) | ('x' <<24);
        fmt_stk_dmp[1] = (':') | (' ' <<8) | (' ' <<16) | ('%' <<24);
        fmt_stk_dmp[2] = ('0') | ('8' <<8) | ('x' <<16) | (' ' <<24);
        fmt_stk_dmp[3] = ('%') | ('0' <<8) | ('8' <<16) | ('x' <<24);
        fmt_stk_dmp[4] = (' ') | ('%' <<8) | ('0' <<16) | ('8' <<24);
        fmt_stk_dmp[5] = ('x') | (' ' <<8) | ('%' <<16) | ('0' <<24);
        fmt_stk_dmp[6] = ('8') | ('x' <<8) | (' ' <<16) | ('%' <<24);
        fmt_stk_dmp[7] = ('c') | ('\n'<<8) | ('\0'<<16) | ('\0'<<24);

        size_t this_mutch = end - start;
        if (this_mutch >= 0x10) {
            for (size_t pos = 0; pos < this_mutch; pos += 0x10) {
                uint32_t *value = (uint32_t *)(start + pos);

                // rough indicator: stack frames usually have SP saved as the second word
                bool looksLikeStackFrame = (value[2] == (start - adjust + pos + 0x10));

                ets_printf((const char*)fmt_stk_dmp, (uint32_t)&value[0] - adjust,
                           value[0], value[1], value[2], value[3],
                           (looksLikeStackFrame)?'<':' ');
            }
        }
    }

    {
        uint32_t fmt_stk_end[4];
        fmt_stk_end[0] = ('<' ) | ('<' <<8) | ('<' <<16) | ('s' <<24);
        fmt_stk_end[1] = ('t' ) | ('a' <<8) | ('c' <<16) | ('k' <<24);
        fmt_stk_end[2] = ('<' ) | ('<' <<8) | ('<' <<16) | ('\n'<<24);
        fmt_stk_end[3] = ('\n') | ('\0'<<8) | ('\0'<<16) | ('\0'<<24);
        ets_printf((const char *)fmt_stk_end);
    }
}

static bool validate_dram_ptr(const void *p) {
    if ((uintptr_t)p >= (uintptr_t)dram_start &&
        (uintptr_t)p <= (uintptr_t)dram_end   &&
        0 == ((uintptr_t)p & 3) ) {
        return true;
    }
    return false;
}
static const uint32_t * ICACHE_RAM_ATTR skip_stackguard(const uint32_t *start, const uint32_t *end, const uint32_t pattern) {
    // First validate address
    if (!validate_dram_ptr(start) ||
        !validate_dram_ptr(end)   ||
        (uintptr_t)start >= (uintptr_t)end) {

        return NULL;
    }

    // Find the end of SYS stack activity
    const uint32_t *uptr = start;

    size_t this_mutch = (UINTPTR_T)end - (UINTPTR_T)start;
    this_mutch /= sizeof(uint32_t);
    size_t i = 0;
    for (; i < this_mutch; i++) {
        if (pattern != uptr[i]) {
            i &= ~3U;
            uptr = &uptr[i];
            break;
        }
    }
    if (i == this_mutch) {
        uptr = &uptr[i];
    }

    return uptr;
}

static void ICACHE_RAM_ATTR handle_hwdt(void) {
    /*
     *  This array is multipurpose:
     *
     *  We create the array through inline assembly by adjusting the stack
     *  pointer just before calling the NONOS SDK. We define the pointers in
     *  advance so we can reference them for generating the stack trace.  The
     *  gap must be great enough that our stack usage now does not run into that
     *  of the SDK.
     *
     *  2. Creates a gap between the beginning of the stack space used by the
     *  Boot ROM and eboot from that used by NONOS SDK Core. By createing an
     *  array on the stack before starting the SDK we put a gap between our
     *  interesting stack activity and that produced by the Boot ROM and eboot.
     *
     *  3. The combined calls made from here and Boot ROM and eboot currently
     *  use 800 bytes of stack. At this time we start with a 1024 byte gap.
     *
     *  4. Once this function starts the SDK. This buffer may be used for other
     *  puposes by referencing g_romStack.
     *
     *  **** I am concern that the 4KEXTRA option may not have enough room left
     *  for SDK SYS stack. For debugging it may be best to use the NO4KEXTRA
     *  option.
     *
     */

// constexpr size_t rom_stack_A16_sz = rom_stack_A16_sz; //MK_ALIGN16_SZ(STACK_SAVER_GAP_SIZE);
// constexpr uint32_t *romStack = romStack; //(uint32_t *)(0x40000000U - rom_stack_A16_sz);

    // We really need to know if it is 1st boot at power on
    bool power_on = false;
    if (//g_sys_stack_first != sys_stack_first ||
        g_romStack != romStack ||
        g_rom_stack_A16_sz != rom_stack_A16_sz ||
#ifdef DEBUG_HWDT_NO4KEXTRA
        g_pcont != &g_cont
#else
        g_pcont != contStack
#endif
   ) {

        power_on = true;
        g_romStack = romStack;
        g_rom_stack_A16_sz = rom_stack_A16_sz;
        // g_sys_stack_first = sys_stack_first;
    }

    ets_memset(&stack_usages, 0, sizeof(stack_usages));

    /*
     *  Detecting a Hardware WDT (HWDT) reset is made a little complicated at
     *  boot before the SDK is started.
     *
     *  While the ROM API will report a HWDT it does not change the status after
     *  a software restart. And the SDK has not been started so its API is not
     *  available.
     *
     *  A value in System RTC memory appears store the reset reason for the SDK.
     *  It appears to be set before the SDK performs its restart. Of course this
     *  value is invalid at power on before the SDK runs.
     *
     *  Case 1: At power on boot the ROM API result is valid; however, the SDK
     *  value in RTC Memory has not been set at this time.
     *
     *  Case 2: A HWDT reset has occured, which is later followed with a
     *  restart by the SDK. At boot the ROM API result still reports the HWDT
     *  reason.
     *
     *  So both results are combined to determine when to generate a stack dump.
     *
     */


    uint32_t rtc_sys_reason = RTC_SYS[0];
    bool hwdt_reset = false;

    if (!power_on && REASON_WDT_RST == rtc_sys_reason) {
        hwdt_reset = true;
    }


    if (!power_on) {
        // // This is pointless this area is zeroed out by the Boot ROM
        // // init code. This part of the sys stack is lost. It is best
        // // to use the NO4KEXTRA option if failure is on the sys stack.
        // const uint32_t *uptr = skip_stackguard(sysStack_e000, sysStack, 0);
        // if ((uintptr_t)uptr == (uintptr_t)sysStack) {
        //     uptr = skip_stackguard(sysStack, romStack, CONT_STACKGUARD);
        // }
        const uint32_t *uptr = skip_stackguard(sysStack, romStack, CONT_STACKGUARD);
        if (uptr) {
            stack_usages.sys = (UINTPTR_T)romStack - (UINTPTR_T)uptr;

            /* Print context SYS */
            if (hwdt_reset) {
                {
                  uint32_t fmt_hwdt[6];
                  fmt_hwdt[0] = ('\n') | ('H' <<8) | ('a' <<16) | ('r' <<24);
                  fmt_hwdt[1] = ('d' ) | ('w' <<8) | ('a' <<16) | ('r' <<24);
                  fmt_hwdt[2] = ('e' ) | (' ' <<8) | ('W' <<16) | ('D' <<24);
                  fmt_hwdt[3] = ('T' ) | (' ' <<8) | ('r' <<16) | ('e' <<24);
                  fmt_hwdt[4] = ('s' ) | ('e' <<8) | ('t' <<16) | ('\n'<<24);
                  fmt_hwdt[5] = 0;
                  ets_printf((const char*)fmt_hwdt);
                }
                print_stack((UINTPTR_T)uptr, (UINTPTR_T)romStack, 0, PART1_SYS | PART2);
                print_size(stack_usages.sys);
            }
        }
#if defined(DEBUG_HWDT_NO4KEXTRA) || defined(STACK_USAGES)
        /* Print separate cont stack */
        uptr = skip_stackguard(g_pcont->stack, g_pcont->stack_end, CONT_STACKGUARD);
        if (uptr) {
            stack_usages.cont = (UINTPTR_T)g_pcont->stack_end - (UINTPTR_T)uptr;
#ifdef DEBUG_HWDT_NO4KEXTRA
            if (stack_usages.cont <= CONT_STACKSIZE) {
                if (hwdt_reset) {
                    print_stack((UINTPTR_T)uptr, (UINTPTR_T)g_pcont->stack_end, 0, PART1_CONT | PART2);
                    print_size(stack_usages.cont);
                }
            }
#endif
        }
#endif
    }

    /*
     *  Fill the SDK stack area with CONT_STACKGUARD so we can detect and
     *  skip the unused section of the stack when printing a Stack Dump.
     */
    {
        size_t this_mutch = (UINTPTR_T)romStack - (UINTPTR_T)sysStack;
        this_mutch /= sizeof(uint32_t);
        for (size_t i = 0; i < this_mutch; i++) {
            sysStack[i] = CONT_STACKGUARD;
        }
    }

#if defined(STACK_USAGES) || defined(ROM_STACK_DUMP)
    /*
     *  Reports on romStack usage by ROM and eboot.
     *  Used to confirm ROM_STACK_SIZE is large enough.
     */
    {
        const uint32_t *uptr = skip_stackguard(romStack, rom_stack_first, CONT_STACKGUARD);
        if (uptr) {
            stack_usages.rom = (UINTPTR_T)rom_stack_first - (UINTPTR_T)uptr;
#if defined(ROM_STACK_DUMP)
            print_stack((UINTPTR_T)uptr, (UINTPTR_T)rom_stack_first, 0, PART1_ROM | PART2);
            print_size(stack_usages.rom);
#endif
        }
    }
#endif
    ets_delay_us(12000); /* Let UART FiFo clear. */
}

void ICACHE_RAM_ATTR app_entry_start(void) {
#ifdef DEBUG_HWDT_NO4KEXTRA
    /*
     *  Continuation context is in BSS.
     */
    g_pcont = &g_cont;
#else
    /*
     *  The continuation context is on the stack just after the reserved space
     *  for the ROM/eboot stack and before the SYS stack begins.
     *  All computations done at top, save pointer to it.
     */
    g_pcont = contStack;
#endif
    /*
     *  Use new calculated SYS stack from top.
     *  Call the entry point of the SDK code.
     */
    asm volatile("" ::: "memory");
    asm volatile ("mov.n a1, %0\n"
                  "mov.n a3, %1\n"
                  "jx a3\n" : : "r" (sys_stack_first), "r" (call_user_start) );

    __builtin_unreachable();
}

void ICACHE_RAM_ATTR app_entry_redefinable(void) {
    handle_hwdt();
    app_entry_start();
    __builtin_unreachable();
}


#if defined(STACK_USAGES)
void initVariant(void) {
    // Fill the romStack while it is not actively being used.
    for (size_t i = 0; i < g_rom_stack_A16_sz/sizeof(uint32_t); i++) {
        g_romStack[i] = CONT_STACKGUARD;
    }
}
#endif

};

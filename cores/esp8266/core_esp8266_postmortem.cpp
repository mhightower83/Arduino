/*
 postmortem.c - output of debug info on sketch crash
 Copyright (c) 2015 Ivan Grokhotkov. All rights reserved.
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


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include "debug.h"
#include "ets_sys.h"
#include "user_interface.h"
#include "esp8266_peri.h"
#include "cont.h"
#include "pgmspace.h"
#include "gdb_hooks.h"
#include "StackThunk.h"
#ifndef xt_rsr_ps
#define xt_rsr_ps()  (__extension__({uint32_t state; __asm__ __volatile__("rsr.ps %0;" : "=a" (state)); state;}))
#endif

extern "C" {

extern void __real_system_restart_local();

#define PS_INVALID_VALUE (0x80000000U)
static struct _PANIC {
// These will be pointers to PROGMEM const strings
    const char* file;
    const char* func;
    const char* what;
    const char* unhandled_exception;
    uint32_t ps_reg;
    int line;
    bool abort_called;
} s_panic = {NULL, NULL, NULL, NULL, PS_INVALID_VALUE, 0, false};

void abort() __attribute__((noreturn));
static void uart_write_char_d(char c);
static void uart0_write_char_d(char c);
static void uart1_write_char_d(char c);
static void print_stack(uint32_t start, uint32_t end);

// From UMM, the last caller of a malloc/realloc/calloc which failed:
extern void *umm_last_fail_alloc_addr;
extern int umm_last_fail_alloc_size;

static void raise_exception() __attribute__((noreturn));

extern void __custom_crash_callback( struct rst_info * rst_info, uint32_t stack, uint32_t stack_end ) {
    (void) rst_info;
    (void) stack;
    (void) stack_end;
}

extern void custom_crash_callback( struct rst_info * rst_info, uint32_t stack, uint32_t stack_end ) __attribute__ ((weak, alias("__custom_crash_callback")));


// Prints need to use our library function to allow for file and function
// to be safely accessed from flash. This function encapsulates snprintf()
// [which by definition will 0-terminate] and dumping to the UART
static void ets_printf_P(const char *str, ...) {
    char destStr[160];
    char *c = destStr;
    va_list argPtr;
    va_start(argPtr, str);
    vsnprintf(destStr, sizeof(destStr), str, argPtr);
    va_end(argPtr);
    while (*c) {
        ets_putc(*(c++));
    }
}

static void crashReport(struct rst_info *rst_info, uint32_t sp_dump, uint32_t offset) {

    // TODO:  ets_install_putc1 definition is wrong in ets_sys.h, need cast
    ets_install_putc1((void *)&uart_write_char_d);

    if (PS_INVALID_VALUE != s_panic.ps_reg)
        ets_printf_P(PSTR("\nPS Register=0x%08X, Interrupts %S\n"), s_panic.ps_reg, (s_panic.ps_reg & 0x0FU)?PSTR("disabled"):PSTR("enabled"));
    if (s_panic.line) {
        ets_printf_P(PSTR("\nPanic %S:%d %S"), s_panic.file, s_panic.line, s_panic.func);
        if (s_panic.what) {
            ets_printf_P(PSTR(": Assertion '%S' failed."), s_panic.what);
        }
        ets_putc('\n');
    }
    else if (s_panic.unhandled_exception) {
        ets_printf_P(PSTR("\nUnhandled C++ exception: %S\n"), s_panic.unhandled_exception);
    }
    else if (s_panic.abort_called) {
        ets_printf_P(PSTR("\nAbort called\n"));
    }
    else if (rst_info->reason == REASON_EXCEPTION_RST) {
            ets_printf_P(PSTR("\nException (%d):\nepc1=0x%08x epc2=0x%08x epc3=0x%08x excvaddr=0x%08x depc=0x%08x\n"),
                rst_info->exccause, rst_info->epc1, rst_info->epc2, rst_info->epc3, rst_info->excvaddr, rst_info->depc);
    }
    else if (rst_info->reason == REASON_SOFT_WDT_RST) {
        ets_printf_P(PSTR("\nSoft WDT reset\n"));
    }

    uint32_t cont_stack_start = (uint32_t) &(g_pcont->stack);
    uint32_t cont_stack_end = (uint32_t) g_pcont->stack_end;
    uint32_t stack_end;

    ets_printf_P(PSTR("\n>>>stack>>>\n"));

    if (sp_dump > stack_thunk_get_stack_bot() && sp_dump <= stack_thunk_get_stack_top()) {
        // BearSSL we dump the BSSL second stack and then reset SP back to the main cont stack
        ets_printf_P(PSTR("\nctx: bearssl\nsp: %08x end: %08x offset: %04x\n"), sp_dump, stack_thunk_get_stack_top(), offset);
        print_stack(sp_dump + offset, stack_thunk_get_stack_top());
        offset = 0; // No offset needed anymore, the exception info was stored in the bssl stack
        sp_dump = stack_thunk_get_cont_sp();
    }

    if (sp_dump > cont_stack_start && sp_dump < cont_stack_end) {
        ets_printf_P(PSTR("\nctx: cont\n"));
        stack_end = cont_stack_end;
    }
    else {
        ets_printf_P(PSTR("\nctx: sys\n"));
        stack_end = 0x3fffffb0;
        // it's actually 0x3ffffff0, but the stuff below ets_run
        // is likely not really relevant to the crash
    }

    ets_printf_P(PSTR("sp: %08x end: %08x offset: %04x\n"), sp_dump, stack_end, offset);

    print_stack(sp_dump + offset, stack_end);

    ets_printf_P(PSTR("<<<stack<<<\n"));

    // Use cap-X formatting to ensure the standard EspExceptionDecoder doesn't match the address
    if (umm_last_fail_alloc_addr) {
      ets_printf_P(PSTR("\nlast failed alloc call: %08X(%d)\n"), (uint32_t)umm_last_fail_alloc_addr, umm_last_fail_alloc_size);
    }

    custom_crash_callback( rst_info, sp_dump + offset, stack_end );
}

void __wrap_system_restart_local() {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;

    if (gdb_present()) {
        /* When GDBStub is present, exceptions are handled by GDBStub,
           but Soft WDT will still call this function.
           Trigger an exception to break into GDB.
           TODO: check why gdb_do_break() or asm("break.n 0") do not
           break into GDB here. */
        raise_exception();  //?? strange - we may be here from a previous call to raise_exception()
    }

    struct rst_info rst_info;
    memset(&rst_info, 0, sizeof(rst_info));
    system_rtc_mem_read(0, &rst_info, sizeof(rst_info));

    // amount of stack taken by interrupt or exception handler
    // and everything up to __wrap_system_restart_local
    // (determined empirically, might break)
    uint32_t offset = 0;
    if (rst_info.reason == REASON_SOFT_WDT_RST) {
        offset = 0x1b0;
    }
    else if (rst_info.reason == REASON_EXCEPTION_RST) {
        offset = 0x1a0;
    }
    else if (rst_info.reason == REASON_WDT_RST) {
        offset = 0x10;
    }
    else {
        return;
        // return ?? why would we not pass on to __real_system_restart_local()
        // What is system_restart_local()? The SDK has a system_restart().
        // I cannot find a definition for system_restart_local().
        // ?? __real_system_restart_local();
    }

    crashReport(&rst_info, sp_dump, offset);

    ets_delay_us(10000);
    __real_system_restart_local();
}


static void print_stack(uint32_t start, uint32_t end) {
    for (uint32_t pos = start; pos < end; pos += 0x10) {
        uint32_t* values = (uint32_t*)(pos);

        // rough indicator: stack frames usually have SP saved as the second word
        bool looksLikeStackFrame = (values[2] == pos + 0x10);

        ets_printf_P(PSTR("%08x:  %08x %08x %08x %08x %c\n"),
            pos, values[0], values[1], values[2], values[3], (looksLikeStackFrame)?'<':' ');
    }
}

static void uart_write_char_d(char c) {
    uart0_write_char_d(c);
    uart1_write_char_d(c);
}

static void uart0_write_char_d(char c) {
    while (((USS(0) >> USTXC) & 0xff)) { }

    if (c == '\n') {
        USF(0) = '\r';
    }
    USF(0) = c;
}

static void uart1_write_char_d(char c) {
    while (((USS(1) >> USTXC) & 0xff) >= 0x7e) { }

    if (c == '\n') {
        USF(1) = '\r';
    }
    USF(1) = c;
}

static void raise_exception() {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;
    s_panic.ps_reg = xt_rsr_ps();
    if (0 != (s_panic.ps_reg & 0x0FU)) {
        // Note, when "syscall" is called with interrupts disable
        // a hardware wdt reset will follow. No stack trace.
        // Need to generate debug info NOW if interrupts are disabled.
        // Note this would have followed the path of "Soft WDT reset".
        // TODO: Feed the hardware WDT so we can get through this process.
        struct rst_info rst_info;
        memset(&rst_info, 0, sizeof(rst_info));
        rst_info.reason = REASON_SOFT_WDT_RST; // Fake it
        crashReport(&rst_info, sp_dump, 0);
    }

    __asm__ __volatile__ ("syscall");
    // Note, observed that when we get called back at __wrap_system_restart_local()
    // PS is 0x023.
    while (1); // never reached, needed to satisfy "noreturn" attribute
}

void abort() {
    s_panic.abort_called = true;
    raise_exception();
}

void __unhandled_exception(const char *str) {
    s_panic.unhandled_exception = str;
    raise_exception();
}

void __assert_func(const char *file, int line, const char *func, const char *what) {
    s_panic.file = file;
    s_panic.line = line;
    s_panic.func = func;
    s_panic.what = what;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception();
}

void __panic_func(const char* file, int line, const char* func) {
    s_panic.file = file;
    s_panic.line = line;
    s_panic.func = func;
    s_panic.what = 0;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception();
}

};

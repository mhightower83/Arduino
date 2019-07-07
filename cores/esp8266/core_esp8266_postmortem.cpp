#if defined(ALT_POSTMORTEM)
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
#include "Esp.h"

// #ifndef xt_rsr_ps
// #define xt_rsr_ps()  (__extension__({uint32_t state; __asm__ __volatile__("rsr.ps %0;" : "=a" (state)::"memory"); state;}))
// #endif

#ifndef __STRINGIFY
#define __STRINGIFY(a) #a
#endif

#ifndef xt_rsr
#define xt_rsr(sr) (__extension__({uint32_t r; __asm__ __volatile__ ("rsr %0," __STRINGIFY(sr) : "=a"(r)::"memory"); r;}))
#endif

#if defined(DEBUG_ESP_PORT) || defined(DEBUG_ESP_ISR)
#define DEBUG_IRAM_ATTR ICACHE_RAM_ATTR
#else
#define DEBUG_IRAM_ATTR
#endif

extern "C" {

extern void __real_system_restart_local();

// From UMM, the last caller of a malloc/realloc/calloc which failed:
extern void *umm_last_fail_alloc_addr;
extern int umm_last_fail_alloc_size;

extern void __custom_crash_callback( struct rst_info * rst_info, uint32_t stack, uint32_t stack_end ) {
    (void) rst_info;
    (void) stack;
    (void) stack_end;
}
extern void custom_crash_callback( struct rst_info * rst_info, uint32_t stack, uint32_t stack_end ) __attribute__ ((weak, alias("__custom_crash_callback")));

#define PS_INVALID_VALUE (0x80000000U)
static struct _PANIC {
    // "const char*" values are pointers to PROGMEM const strings
    const char* file;
    const char* func;
    const char* what;
    const char* unhandled_exception;
    uint32_t ps_reg;
    int line;
    bool abort_called;
} s_panic = {NULL, NULL, NULL, NULL, PS_INVALID_VALUE, 0, false};

void abort() __attribute__((noreturn));
static void print_stack(uint32_t start, uint32_t end);
static void raise_exception(bool early_reporting) __attribute__((noreturn));

/*
    Print routines that can safely print with interrupts disabled
    and free of "malloc lib" calls.
*/

// ROM _putc1, ignores CRs and sends CR/LF for LF, newline.
// Always returns character sent.
extern "C" int constexpr (*_putc1)(int) = (int (*)(int))0x40001dcc;
#undef putc
#define putc _putc1

extern "C" void uart_buff_switch(uint8_t);

void inline _select_dbg_serial(void) {
#ifdef DEBUG_ESP_PORT
#define VALUE(x) __STRINGIFY(x)
    // Preprocessor and compiler together will optimize away the if.
    if (strcmp("Serial1", VALUE(DEBUG_ESP_PORT)) == 0) {
        uart_buff_switch(1U);
    } else {
        uart_buff_switch(0U);
    }
#else
   uart_buff_switch(0U); // Side effect, clears RX FIFO
#endif
}

// #define SZ_METHOD

#ifdef SZ_METHOD
static int DEBUG_IRAM_ATTR _sz_puts_P(const size_t buf_len, const char *fmt) {
    _select_dbg_serial();
    char ram_buf[buf_len] __attribute__ ((aligned(4)));
    ets_memcpy(ram_buf, fmt, buf_len);
    const char *pS = ram_buf;
    char c;
    while( (c=*pS++) ) _putc1(c);
    return 1;
}

#undef puts_P
static int DEBUG_IRAM_ATTR puts_P(const char *fmt) {
    size_t szLen = ets_strlen(fmt) + 1;
    size_t buf_len = (szLen + 3) & ~0x03U;
    return _sz_puts_P(buf_len, fmt);
}

#undef puts
#define puts(str) _sz_puts_P((sizeof(str) + 3) & ~0x03U, PSTR(str))

#else // ! SZ_METHOD

#undef puts_P
static int DEBUG_IRAM_ATTR puts_P(const char *fmt) {
    _select_dbg_serial();
    size_t szLen = ets_strlen(fmt) + 1;
    size_t buf_len = (szLen + 3) & ~0x03U;
    char ram_buf[buf_len] __attribute__ ((aligned(4)));
    ets_memcpy(ram_buf, fmt, buf_len);
    const char *pS = ram_buf;
    char c;
    while( (c=*pS++) ) _putc1(c);
    return 1;
}
#undef puts
#define puts(str) puts_P(PSTR(str))
#endif

#ifdef SZ_METHOD
static int DEBUG_IRAM_ATTR _pmsz_printf_P(const size_t buf_len, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int DEBUG_IRAM_ATTR _pmsz_printf_P(const size_t buf_len, const char *fmt, ...) {
    _select_dbg_serial();
    WDT_FEED();
    char __aligned(4) ram_buf[buf_len];
    ets_memcpy(ram_buf, fmt, buf_len);
    va_list argPtr;
    va_start(argPtr, fmt);
    int result = ets_vprintf(_putc1, ram_buf, argPtr);
    va_end(argPtr);
    return result;
}
#undef printf
#define printf(fmt, ...) _pmsz_printf_P((sizeof(fmt) + 3) & ~0x03U, PSTR(fmt), ##__VA_ARGS__)

#else  // ! SZ_METHOD

#undef _printf_P
static int DEBUG_IRAM_ATTR _pm_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int DEBUG_IRAM_ATTR _pm_printf_P(const char *fmt, ...) {
    _select_dbg_serial();
    WDT_FEED();
    size_t szLen = ets_strlen(fmt) + 1;
    size_t buf_len = (szLen + 3) & ~0x03U;
    char ram_buf[buf_len] __attribute__ ((aligned(4)));
    ets_memcpy(ram_buf, fmt, buf_len);
    va_list argPtr;
    va_start(argPtr, fmt);
    int result = ets_vprintf(_putc1, ram_buf, argPtr);
    va_end(argPtr);
    return result;
}
#undef printf
#define printf(fmt, ...) _pm_printf_P(PSTR(fmt), ##__VA_ARGS__)
#endif // #ifdef SZ_METHOD

static void DEBUG_IRAM_ATTR crashReport(struct rst_info *rst_info, uint32_t sp_dump,
                        uint32_t offset, bool custom_crash_cb_enabled) {

    if (PS_INVALID_VALUE != s_panic.ps_reg) {
        printf("\nPS Register=0x%03X, Interrupts ", s_panic.ps_reg);
        if(s_panic.ps_reg & 0x0FU)
            puts("disabled\n");
        else
            puts("enabled\n");
    }
    if (s_panic.line) {
        puts("\nPanic ");
        puts_P(s_panic.file);
        printf(":%d ", s_panic.line);
        puts_P(s_panic.func);
        if (s_panic.what) {
            puts(": Assertion ");
            puts_P(s_panic.what);
            puts(" failed.");
        }
        putc('\n');
    }
    else if (s_panic.unhandled_exception) {
        puts("\nUnhandled C++ exception: ");
        puts_P(s_panic.unhandled_exception);
        putc('\n');
    }
    else if (s_panic.abort_called) {
        puts("\nAbort called\n");
    }
    else if (rst_info->reason == REASON_EXCEPTION_RST) {
        printf("\nException (%d):\nepc1=0x%08x epc2=0x%08x"
                " epc3=0x%08x excvaddr=0x%08x depc=0x%08x\n",
                rst_info->exccause, rst_info->epc1, rst_info->epc2,
                rst_info->epc3, rst_info->excvaddr, rst_info->depc);
    }
    else if (rst_info->reason == REASON_SOFT_WDT_RST) {
        puts("\nSoft WDT reset\n");
    }

    uint32_t cont_stack_start = (uint32_t) &(g_pcont->stack);
    uint32_t cont_stack_end = (uint32_t) g_pcont->stack_end;
    uint32_t stack_end;

    puts("\n>>>stack>>>\n");

    if (sp_dump > stack_thunk_get_stack_bot() && sp_dump <= stack_thunk_get_stack_top()) {
        // BearSSL we dump the BSSL second stack and then reset SP back to the main cont stack
        printf("\nctx: bearssl\nsp: %08x end: %08x offset: %04x\n", sp_dump, stack_thunk_get_stack_top(), offset);
        print_stack(sp_dump + offset, stack_thunk_get_stack_top());
        offset = 0; // No offset needed anymore, the exception info was stored in the bssl stack
        sp_dump = stack_thunk_get_cont_sp();
    }

    if (sp_dump > cont_stack_start && sp_dump < cont_stack_end) {
        puts("\nctx: cont\n");
        stack_end = cont_stack_end;
    }
    else {
        puts("\nctx: sys\n");
        stack_end = 0x3fffffb0;
        // it's actually 0x3ffffff0, but the stuff below ets_run
        // is likely not really relevant to the crash
    }

    printf("sp: %08x end: %08x offset: %04x\n", sp_dump, stack_end, offset);

    print_stack(sp_dump + offset, stack_end);

    puts("<<<stack<<<\n");

    // Use cap-X formatting to ensure the standard EspExceptionDecoder doesn't match the address
    if (umm_last_fail_alloc_addr) {
        printf("\nlast failed alloc call: %08X(%d)\n", (uint32_t)umm_last_fail_alloc_addr, umm_last_fail_alloc_size);
    }

    if (custom_crash_cb_enabled)
        custom_crash_callback( rst_info, sp_dump + offset, stack_end );
}

void DEBUG_IRAM_ATTR __wrap_system_restart_local() {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;

    if (gdb_present()) {
        /* When GDBStub is present, exceptions are handled by GDBStub,
           but Soft WDT will still call this function.
           Trigger an exception to break into GDB.
           TODO: check why gdb_do_break() or asm("break.n 0") do not
           break into GDB here. */
        __asm__ __volatile__ ("syscall"); // moved in from raise_exception();
        while (1); // never reached, needed to satisfy "noreturn" attribute
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

    crashReport(&rst_info, sp_dump, offset, true);

    ets_delay_us(10000);
    __real_system_restart_local();
}

static void DEBUG_IRAM_ATTR print_stack(uint32_t start, uint32_t end) {
    for (uint32_t pos = start; pos < end; pos += 0x10) {
        uint32_t* values = (uint32_t*)(pos);

        // rough indicator: stack frames usually have SP saved as the second word
        bool looksLikeStackFrame = (values[2] == pos + 0x10);

        printf("%08x:  %08x %08x %08x %08x %c\n",
            pos, values[0], values[1], values[2], values[3], (looksLikeStackFrame)?'<':' ');
    }
}

static void DEBUG_IRAM_ATTR fill_rst_info(struct rst_info *rst_info) {
#if 0
    memset(rst_info, 0, sizeof(struct rst_info));
#else
    rst_info->exccause = xt_rsr(EXCCAUSE);
    rst_info->epc1 = xt_rsr(EPC1);
    rst_info->epc2 = xt_rsr(EPC2);
    rst_info->epc3 = xt_rsr(EPC3);
    rst_info->excvaddr = xt_rsr(EXCVADDR);
    rst_info->depc = xt_rsr(DEPC);
#endif
    rst_info->reason = REASON_SOFT_WDT_RST; // Fake it =3
}
static void DEBUG_IRAM_ATTR raise_exception(bool early_reporting) {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;
    s_panic.ps_reg = xt_rsr(ps);
    // if (early_reporting || 0 != (s_panic.ps_reg & 0x0FU)) {
        // If interrupts are disabled, must generate debug info NOW.
        // Note this would have followed the path of "Soft WDT reset".
        // so make this look like a "Soft WDT reset"
        struct rst_info rst_info;
        fill_rst_info(&rst_info);
        system_rtc_mem_write(0, &rst_info, sizeof(rst_info));
        crashReport(&rst_info, sp_dump, 0, true);

        // We need an exit that will cause the system to reboot.
        // Maybe call __real_system_restart_local(); It waits for the tx FIFOs
        // to empty and other stuff before calling system_restart_core().
        // It looks like the right way to handle this.
        __real_system_restart_local(); // Should not return.
    // }
    //
    // __asm__ __volatile__ ("syscall");
    while (1); // never reached, needed to satisfy "noreturn" attribute
}

void DEBUG_IRAM_ATTR abort() {
    s_panic.abort_called = true;
    raise_exception(true);
}

void DEBUG_IRAM_ATTR __unhandled_exception(const char *str) {
    s_panic.unhandled_exception = str;
    raise_exception(true);
}

void DEBUG_IRAM_ATTR __assert_func(const char *file, int line, const char *func, const char *what) {
    s_panic.file = file;
    s_panic.line = line;
    s_panic.func = func;
    s_panic.what = what;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception(true);
}

void DEBUG_IRAM_ATTR __panic_func(const char* file, int line, const char* func) {
    s_panic.file = file;
    s_panic.line = line;
    s_panic.func = func;
    s_panic.what = 0;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception(true);
}

// Typical call:  inflight_stack_trace(xt_rsr(ps));
void DEBUG_IRAM_ATTR inflight_stack_trace(uint32_t ps_reg) {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;
    s_panic.ps_reg = ps_reg;
    struct rst_info rst_info;
    fill_rst_info(&rst_info);
    puts("\nPostmortem Infligth Stack Trace\n");
    crashReport(&rst_info, sp_dump, 0, false);
}

};
#else
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

extern "C" {

extern void __real_system_restart_local();

// These will be pointers to PROGMEM const strings
static const char* s_panic_file = 0;
static int s_panic_line = 0;
static const char* s_panic_func = 0;
static const char* s_panic_what = 0;

static bool s_abort_called = false;
static const char* s_unhandled_exception = NULL;

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

void __wrap_system_restart_local() {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;

    if (gdb_present()) {
        /* When GDBStub is present, exceptions are handled by GDBStub,
           but Soft WDT will still call this function.
           Trigger an exception to break into GDB.
           TODO: check why gdb_do_break() or asm("break.n 0") do not
           break into GDB here. */
        raise_exception();
    }

    struct rst_info rst_info;
    memset(&rst_info, 0, sizeof(rst_info));
    system_rtc_mem_read(0, &rst_info, sizeof(rst_info));
    if (rst_info.reason != REASON_SOFT_WDT_RST &&
        rst_info.reason != REASON_EXCEPTION_RST &&
        rst_info.reason != REASON_WDT_RST)
    {
        return;
    }

    // TODO:  ets_install_putc1 definition is wrong in ets_sys.h, need cast
    ets_install_putc1((void *)&uart_write_char_d);

    if (s_panic_line) {
        ets_printf_P(PSTR("\nPanic %S:%d %S"), s_panic_file, s_panic_line, s_panic_func);
        if (s_panic_what) {
            ets_printf_P(PSTR(": Assertion '%S' failed."), s_panic_what);
        }
        ets_putc('\n');
    }
    else if (s_unhandled_exception) {
        ets_printf_P(PSTR("\nUnhandled C++ exception: %S\n"), s_unhandled_exception);
    }
    else if (s_abort_called) {
        ets_printf_P(PSTR("\nAbort called\n"));
    }
    else if (rst_info.reason == REASON_EXCEPTION_RST) {
        ets_printf_P(PSTR("\nException (%d):\nepc1=0x%08x epc2=0x%08x epc3=0x%08x excvaddr=0x%08x depc=0x%08x\n"),
            rst_info.exccause, rst_info.epc1, rst_info.epc2, rst_info.epc3, rst_info.excvaddr, rst_info.depc);
    }
    else if (rst_info.reason == REASON_SOFT_WDT_RST) {
        ets_printf_P(PSTR("\nSoft WDT reset\n"));
    }

    uint32_t cont_stack_start = (uint32_t) &(g_pcont->stack);
    uint32_t cont_stack_end = (uint32_t) g_pcont->stack_end;
    uint32_t stack_end;

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

    custom_crash_callback( &rst_info, sp_dump + offset, stack_end );

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
    __asm__ __volatile__ ("syscall");
    while (1); // never reached, needed to satisfy "noreturn" attribute
}

void abort() {
    s_abort_called = true;
    raise_exception();
}

void __unhandled_exception(const char *str) {
    s_unhandled_exception = str;
    raise_exception();
}

void __assert_func(const char *file, int line, const char *func, const char *what) {
    s_panic_file = file;
    s_panic_line = line;
    s_panic_func = func;
    s_panic_what = what;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception();
}

void __panic_func(const char* file, int line, const char* func) {
    s_panic_file = file;
    s_panic_line = line;
    s_panic_func = func;
    s_panic_what = 0;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception();
}

};
#endif

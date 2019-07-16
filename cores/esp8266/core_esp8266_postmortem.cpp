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
// #include "Esp.h"

#ifndef __STRINGIFY
#define __STRINGIFY(a) #a
#endif

#ifndef xt_rsr
#define xt_rsr(sr) (__extension__({uint32_t r; __asm__ __volatile__ ("rsr %0," __STRINGIFY(sr) : "=a"(r)::"memory"); r;}))
#endif

#if 0
// Start of ISR debug support .h file - tied to ALT_POSTMORTEM
extern "C" {
  // Print 1 character, ignore '\r' and print "\r\n" when '\n' detected.
  int constexpr (*_putc1)(int);
  #undef putc
  #define putc _putc1

  int _pm_puts_P(const char *fmt);
  #undef puts_P
  // Print null terminated string. Takes aligned(4) address of PROGMEM string.
  #define puts_P(str) _pm_puts_P(str)

  #undef puts
  // Print null terminated string. Specified string is stored in PROGMEM.
  #define puts(str) _pm_puts_P(PSTR(str))

  int _pm_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
  #undef printf
  #undef printf_P
  #define printf_P(fmt, ...) _pm_printf_P(fmt, ##__VA_ARGS__)
  #define printf(fmt, ...) _pm_printf_P(PSTR(fmt), ##__VA_ARGS__)

  void inflight_stack_trace(uint32_t ps_reg); // use xt_rsr(PS) for arg1
};
// End of .h file
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

// Keeping module static variables together in structure allows the use of
// processor base pointer instructions and reduces IRAM size.
static struct _PANIC {
    // "const char*" values are pointers to PROGMEM const strings
    const char* file;
    const char* func;
    const char* what;
    const char* unhandled_exception;
    uint32_t ps_reg;
    int line;
    bool abort_called;
}  s_panic = {NULL, NULL, NULL, NULL, 0, 0, false};

void abort() __attribute__((noreturn));
static void print_stack(uint32_t start, uint32_t end);
static void raise_exception(void) __attribute__((noreturn));

/*
    Print routines that can safely print with interrupts disabled
    and free of "malloc lib" calls.
*/

#if defined(DEBUG_ESP_PORT) || defined(DEBUG_ESP_ISR)

// Print 1 character, ignore '\r' and print "\r\n" when '\n' detected.
// ROM _putc1, Always returns character sent.
int constexpr (*_rom_putc1)(int) = (int (*)(int))0x40001dcc;
#undef putc
#define putc _rom_putc1


int _isr_safe_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#undef printf
#undef printf_P
#define printf_P(fmt, ...) _isr_safe_printf_P(fmt, ##__VA_ARGS__)
#define printf(fmt, ...) _isr_safe_printf_P(PSTR(fmt), ##__VA_ARGS__)

#undef puts_P
#define puts_P(str) _isr_safe_printf_P(str)
#undef puts
#define puts(str) _isr_safe_printf_P(PSTR(str))

#else

extern "C" void uart_buff_switch(uint8_t);

// ROM _putc1, ignores CRs and sends CR/LF for LF, newline.
// Always returns character sent.
extern "C" int constexpr (*_putc1)(int) = (int (*)(int))0x40001dcc;
#undef putc
#define putc _putc1

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

int DEBUG_IRAM_ATTR _pm_puts_P(const char *fmt) {
    _select_dbg_serial();
    size_t str_len = ets_strlen(fmt);
    size_t buf_len = (str_len + 1 + 3) & ~0x03U;
    char ram_buf[buf_len] __attribute__ ((aligned(4)));
    ets_memcpy(ram_buf, fmt, buf_len);
    const char *pS = ram_buf;
    char c;
    while( (c=*pS++) ) _putc1(c);
    return str_len;
}
#undef puts_P
#define puts_P(str) _pm_puts_P(str)
#undef puts
#define puts(str) _pm_puts_P(PSTR(str))

int DEBUG_IRAM_ATTR _pm_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int DEBUG_IRAM_ATTR _pm_printf_P(const char *fmt, ...) {
    _select_dbg_serial();
    WDT_FEED();
    size_t str_len = ets_strlen(fmt);
    size_t buf_len = (str_len + 1 + 3) & ~0x03U;
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
#endif

static void DEBUG_IRAM_ATTR print_crash_report(struct rst_info *rst_info,
          uint32_t sp_dump, uint32_t offset, bool custom_crash_cb_enabled) {

#ifdef DEBUG_ESP_ISR
    if (0 != s_panic.ps_reg) {
        printf("\nPS Register=0x%03X, Interrupts ", s_panic.ps_reg);
        if(s_panic.ps_reg & 0x0FU) {
            if ((uint32_t)custom_crash_callback >= 0x40200000)
                custom_crash_cb_enabled = false;
            puts("disabled\n");
        } else {
            puts("enabled\n");
        }
    }
#endif
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
/* Notes to self:
Begin fictional documenation of System Restart.

An outline of how it might work

From some where `system_restart` or `system_restart_local()`is called.
eg. *((int*)0) = 0

`system_restart()` (FLASH)
  * shutdown wifi interface
  * shutdown wifi timer callbacks
  * Start a callback timer to run `system_restart_local()` once, after 100ms delay.
  * returns to caller
    * SDK notes: "... do not call other functions after calling this API"

`system_restart_local()` (FLASH) - Runs from a timer callback or called
directly from a Soft WDT call or Startup, maybe some more indirect paths!
  * Handle some strange stuff
  * Grab a copy of restart info from RTC memory.
  * Keep restart info reason REASON_EXCEPTION_RST or REASON_SOFT_WDT_RST
    for all others:
      * zero restart info
      * set reason to REASON_SOFT_RESTART.
      * Update RTC.
  * Call `system_restart_hook(&rst_inf)`
  * wait for uart TX fifo's to empty
  * Enter lock, do some hardware stuff
  * call `system_restart_core()` - ps.intlevel still at 3

`system_restart_core()` (IRAM) - This is the end of the line
  * Wait for SPI flashchip to idle down
  * Disable read cache.
  * pass control to reset vector

End of fictional documenation.

Note, we have a linker wrapper around system_restart_local. And when the reason
does not match our list we return. The real system_restart_local does not have
a return it calls system_restart_core(0) which finalizes the reboot process.
It looks like postmortem's path is aborted the current processing path.
Some other event will have to occur to finish the restart.

Of course this is all based on an interpetation of a psuedo C interpetation of
disassembled 1.x SDK.

*/
void DEBUG_IRAM_ATTR __wrap_system_restart_local() {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;

    if (gdb_present()) {
        /* When GDBStub is present, exceptions are handled by GDBStub,
           but Soft WDT will still call this function.
           Trigger an exception to break into GDB.
           TODO: check why gdb_do_break() or asm("break.n 0") do not
           break into GDB here. */
#if 1
        // These two lines moved in from raise_exception()
        __asm__ __volatile__ ("syscall"); // moved in from
        while (1); // never reached, needed to satisfy "noreturn" attribute
#else
        // this looping will grow the stack usage.
        raise_exception();
#endif
    }

    struct rst_info rst_info;
    memset(&rst_info, 0, sizeof(rst_info));
    system_rtc_mem_read(0, &rst_info, sizeof(rst_info));

    // amount of stack taken by interrupt or exception handler
    // and everything up to __wrap_system_restart_local
    // (determined empirically, might break)
    uint32_t offset = 0;
    bool interesting = true;
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
        // This way for:
        //   Power on - not likely
        //   Software/System restart *
        //   Deep-Sleep Wake *
        //   External System reset
        //
        // * these are the ones I think may pass by here.
        printf("\nSystem restart reason: %d\n", rst_info.reason);
        interesting = false;
    }

    if (interesting)
        print_crash_report(&rst_info, sp_dump, offset, true);

    ets_delay_us(10000);
    __real_system_restart_local();
}

static void DEBUG_IRAM_ATTR print_stack(uint32_t start, uint32_t end) {
    for (uint32_t pos = start; pos < end; pos += 0x10) {
        uint32_t* values = (uint32_t*)(pos);

        WDT_FEED();  // ISR safe - macro, performs volatile memory writes inline

        // rough indicator: stack frames usually have SP saved as the second word
        bool looksLikeStackFrame = (values[2] == pos + 0x10);

        printf("%08x:  %08x %08x %08x %08x %c\n",
            pos, values[0], values[1], values[2], values[3], (looksLikeStackFrame)?'<':' ');
    }
}

#if 1
#ifndef xt_rsil
#define xt_rsil(level) (__extension__({uint32_t state; __asm__ __volatile__("rsil %0," __STRINGIFY(level) : "=a" (state)); state;}))
#endif
static void DEBUG_IRAM_ATTR raise_exception() {
/*
  This is old logic flow that I saw with `raise_exception()`:

  For ps.intlevel = 0:
    * syscall - has not visable effect - call and returns.
    * while(1) - sit and wait for soft wdt
    * hit soft wdt - system passes control back to __wrap_system_restart_local
    * postmortem prints everyone is happy

  For ps.intlevel != 0:
    * syscall - has not visable effect - call and returns.
    * while(1) - sit and wait for soft wdt
    * hardware WDT stikes, __wrap_system_restart_local is never called
    * postmortem never prints everyone is sad

*/
    printf("\nStarting to raise an exception to effect stack trace.\n");
#ifdef DEBUG_ESP_ISR
    s_panic.ps_reg = xt_rsr(ps);
#endif
    // __asm__ __volatile__ ("syscall"); This doesn't do anything I can see.
    *((int*)0) = 0;

    // This was used to cause the Soft WDT event.
    while (1); // never reached, needed to satisfy "noreturn" attribute
}


#else


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

/*
  A more direct handling approach
*/
static void DEBUG_IRAM_ATTR raise_exception(void) {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;
#ifdef DEBUG_ESP_ISR
    s_panic.ps_reg = xt_rsr(ps);
#endif
    // Just in case interrupts are disabled, must generate debug info NOW. Note
    // this would have followed the path of "Soft WDT reset". So make this look
    // like a "Soft WDT reset"
    struct rst_info rst_info;
    fill_rst_info(&rst_info);
    system_rtc_mem_write(0, &rst_info, sizeof(rst_info));
    print_crash_report(&rst_info, sp_dump, 0, true);

    // We need an exit that will cause the system to reboot.
    // Maybe call __real_system_restart_local(); It waits for the tx FIFOs
    // to empty and other stuff before calling system_restart_core().
    // It looks like the right way to handle this.
    ets_delay_us(10000);
    /*
      look at what prep is needed before calling this
        maybe have a flag so __wrap_system_restart_local will pass straignt to
        __real_system_restart_local.
        Then we can just do a simpel system_restart - maybe
    */
    __real_system_restart_local(); // Should not return.
    while (1); // never reached, needed to satisfy "noreturn" attribute
}
#endif



void DEBUG_IRAM_ATTR abort() {
    s_panic.abort_called = true;
    raise_exception();
}

void DEBUG_IRAM_ATTR __unhandled_exception(const char *str) {
    s_panic.unhandled_exception = str;
    raise_exception();
}

void DEBUG_IRAM_ATTR __assert_func(const char *file, int line, const char *func, const char *what) {
    s_panic.file = file;
    s_panic.line = line;
    s_panic.func = func;
    s_panic.what = what;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception();
}

void DEBUG_IRAM_ATTR __panic_func(const char* file, int line, const char* func) {
    s_panic.file = file;
    s_panic.line = line;
    s_panic.func = func;
    s_panic.what = 0;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception();
}

#ifdef DEBUG_ESP_ISR
void DEBUG_IRAM_ATTR inflight_stack_trace(uint32_t ps_reg) {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;
    s_panic.ps_reg = ps_reg; //xt_rsr(PS);
    struct rst_info rst_info;
    fill_rst_info(&rst_info);
    puts("\nPostmortem Infligth Stack Trace\n");
    print_crash_report(&rst_info, sp_dump, 0, false);
}
#endif
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

#ifndef xt_rsr
#define xt_rsr(sr) (__extension__({uint32_t r; __asm__ __volatile__ ("rsr %0," __STRINGIFY(sr) : "=a"(r)::"memory"); r;}))
#endif

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

// using numbers different from "REASON_" in user_interface.h (=0..6)
enum rst_reason_sw
{
    REASON_USER_SWEXCEPTION_RST = 254
};
static int s_user_reset_reason = REASON_DEFAULT_RST;
static const char* s_user_reset_reason_str = 0;

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

    struct rst_info rst_info;
    memset(&rst_info, 0, sizeof(rst_info));
    if (s_user_reset_reason == REASON_DEFAULT_RST)
    {
        system_rtc_mem_read(0, &rst_info, sizeof(rst_info));
        if (rst_info.reason != REASON_SOFT_WDT_RST &&
            rst_info.reason != REASON_EXCEPTION_RST &&
            rst_info.reason != REASON_WDT_RST)
        {
            rst_info.reason = REASON_DEFAULT_RST;
        }
    }
    else
        rst_info.reason = s_user_reset_reason;

    // TODO:  ets_install_putc1 definition is wrong in ets_sys.h, need cast
    ets_install_putc1((void *)&uart_write_char_d);

    if (s_user_reset_reason_str) {
        ets_printf_P(s_user_reset_reason_str);
    }

    if (rst_info.reason != REASON_DEFAULT_RST) {
        ets_printf_P(PSTR("\nPS register: 0x02X\n"), xt_rsr(PS));
    }

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
    else {
        ets_printf_P(PSTR("\nGeneric Reset\n"));
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
    if (gdb_present())
        __asm__ __volatile__ ("syscall"); // triggers GDB when enabled

    s_user_reset_reason = REASON_USER_SWEXCEPTION_RST;
    s_user_reset_reason_str = PSTR("\nUser exception (panic/abort/assert)");
    __wrap_system_restart_local();

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

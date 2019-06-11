#if 0
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
#include "inflight_dbg.h"

#ifndef xt_rsr_ps
#define xt_rsr_ps()  (__extension__({uint32_t state; __asm__ __volatile__("rsr.ps %0;" : "=a" (state)); state;}))
#endif

#define dbg_printf inflight_printf

extern "C" {

  #if 1 //defined(DEBUG_ESP_PORT) && ( defined(DEBUG_ESP_CORE) || defined(DEBUG_ESP_OOM) )
  /*
    Objective:  To be able to print "last gasp" diagnostic messages
    when interrupts are disabled and w/o availability of heap resources.
  */

  void system_soft_wdt_feed(void); // <user_interface.h>
  constexpr uint32_t WDT_TIME_TO_FEED = (400000UL*( F_CPU / 1000000UL )); // 0.4 secs.
  static uint32_t wdt_last_feeding;
  #if !(UMM_ALT_CRITICAL_METHOD == 2)
  static inline uint32_t GetCycleCount() {
    uint32_t ccount;
    __asm__ __volatile__("esync; rsr %0,ccount":"=a"(ccount));
    return ccount;
  }
  #endif

  // ROM _putc1, ignores CRs and sends CR/LF for LF, newline.
  // Always returns character sent.
  constexpr int (*_putc1)(int) = (int (*)(int))0x40001dcc;
  void uart_buff_switch(uint8_t);

  static int ICACHE_RAM_ATTR _local_putc(int c) {
    if ((GetCycleCount() - wdt_last_feeding) >= WDT_TIME_TO_FEED) {
      system_soft_wdt_feed();
      wdt_last_feeding = GetCycleCount();
    }
    return _putc1(c);
  }

  int _sz_printf_P(const size_t buf_len, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  int _sz_printf_P(const size_t buf_len, const char *fmt, ...) {
    system_soft_wdt_feed();
    wdt_last_feeding = GetCycleCount();

  #ifdef DEBUG_ESP_PORT
  #define VALUE(x) __STRINGIFY(x)
    // Preprocessor and compiler combined will optomize away the if.
    if (strcmp("Serial1", VALUE(DEBUG_ESP_PORT)) == 0) {
      uart_buff_switch(1U);
    } else {
      uart_buff_switch(0U);
    }
  #else
    uart_buff_switch(0); // This will clear RX FIFO
  #endif

    char __aligned(4) ram_buf[buf_len];
    ets_memcpy(ram_buf, fmt, buf_len);
    va_list argPtr;
    va_start(argPtr, fmt);
    int result = ets_vprintf(&_local_putc, ram_buf, argPtr);
    va_end(argPtr);
    return result;
  }

  #define printf(fmt, ...) _sz_printf_P((sizeof(fmt) + 3) & ~0x03U, PSTR(fmt), ##__VA_ARGS__)
  #else

  // #define printf(fmt, ...) do { } while (false)
  #define printf(fmt, ...) \
    do {  \
      static const char fstr[] PROGMEM = fmt; \
      char rstr[((sizeof(fmt) + 3) & ~0x03U)]; \
      os_memcpy(rstr, fstr, sizeof(rstr)); \
      printf(rstr, ##__VA_ARGS__); \
    } while (0)
  #endif

inline bool interlocked_exchange_bool(bool *target, bool value) {
    uint32_t ps = xt_rsil(15);
    bool oldValue = *target;
    *target = value;
    xt_wsr_ps(ps);
    return oldValue;
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

static void uart_write_char_d(char c) {
    uart0_write_char_d(c);
    uart1_write_char_d(c);
}

static bool install_putc1 = true;

#define WDT_TIME_TO_FEED (500000*clockCyclesPerMicrosecond())
// Prints need to use our library function to allow for file and function
// to be safely accessed from flash. This function encapsulates snprintf()
// [which by definition will 0-terminate] and dumping to the UART.
int inflight_printf(const char *str, ...) {
    static bool busy = false;
    if (interlocked_exchange_bool(&busy, true))
        return -1;

    if (install_putc1) {
        // TODO:  ets_install_putc1 definition is wrong in ets_sys.h, need cast
        ets_install_putc1((void *)&uart_write_char_d);
        install_putc1 = false;
    }
    // char destStr[160];
    va_list argPtr;
    va_start(argPtr, str);
    int destStrSz = vsnprintf(NULL, 0, str, argPtr);
    va_end(argPtr);
    if (0 >= destStrSz) {
        interlocked_exchange_bool(&busy, false);
        return destStrSz;
    }

    char destStr[destStrSz + 1];
    char *c = destStr;
    va_start(argPtr, str);
    vsnprintf(destStr, destStrSz + 1, str, argPtr);
    va_end(argPtr);

    system_soft_wdt_feed();
    uint32_t wdt_last_feeding = ESP.getCycleCount();
    while (*c) {
        ets_putc(*(c++));
        // If we are printing a lot, make sure we get to finish.
        if ((ESP.getCycleCount() - wdt_last_feeding) >= WDT_TIME_TO_FEED) {
            system_soft_wdt_feed();
            wdt_last_feeding = ESP.getCycleCount();
        }
    }
    interlocked_exchange_bool(&busy, false);
    return destStrSz;
}

#if 0
// Typical call:  inflight_stack_trace(xt_rsr_ps());
void inflight_stack_trace(uint32_t ps_reg) {
  register uint32_t sp asm("a1");
  uint32_t sp_dump = sp;
  s_panic.ps_reg = ps_reg;
  struct rst_info rst_info;
  memset(&rst_info, 0, sizeof(rst_info));
  rst_info.reason = REASON_SOFT_WDT_RST; // Fake it
  postmortem_printf(PSTR("\nPostmortem Infligth Stack Trace\n"));
  crashReport(&rst_info, sp_dump, 0, false);
}
#endif

}; //extern "C" {


uint32_t PrintExecutionTime::_calibrationCycles=0;

PrintExecutionTime::PrintExecutionTime(const char * txtIdentifier, const uint32_t threshold)
  : _txt(txtIdentifier)
  , _threshold(threshold)
  {
  if (_calibrationCycles == 0) {
    calibrate();
  }

  _startCycle = ESP.getCycleCount();
}

PrintExecutionTime::~PrintExecutionTime() {
  uint32_t _elapsedTime = (ESP.getCycleCount() - _startCycle - _calibrationCycles)
                          /clockCyclesPerMicrosecond();
  dbg_printf(_txt);
  dbg_printf(PSTR(" execution time: %u us\n"), _elapsedTime);
}

uint32_t PrintExecutionTime::calibrate(uint32_t cal) {
    if (cal == 0)  {
        uint32_t startCycle = ESP.getCycleCount();
        _calibrationCycles = ESP.getCycleCount() - startCycle;
    } else {
        _calibrationCycles =  cal;
    }
    return _calibrationCycles;
}



#endif

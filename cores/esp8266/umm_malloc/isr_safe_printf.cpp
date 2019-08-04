/*
 * isr_safe_printf.cpp - Intended for diagnostic printing from a restricted setting.
 *
 * Meant to be a print from anywhere and work. There will be more limitations
 * than your typical printf function.
 *
 * Still researching options for printing.
 */

/*
  Printing from the malloc routines is tricky. Since a print library
  might call *alloc. Then recusion may follow as each error call may fail
  into another error and so on.

  Objective:  To be able to print "last gasp" diagnostic messages
  when interrupts are disabled and w/o availability of heap resources.

  Considerations:
  * can be called from ISR
  * can be called from malloc code, cannot use malloc
  * can be called from malloc code that was called from an ISR
  * can be called from with in a critical section, eg. xt_rsil(15);
    * this may be effectively the same as being called from an ISR?

  Knowns:
  * ets_printf - in ROM is not safe corrupts heap, this was meantioned in
    rtos SDK. Rtos SDK corrects this by redefining ROM address for ets_vprintf
    to ets_io_vprintf through LD table and creates a traditional vprintf
    function using it.
    Rtos SDK replaces ets_printf with local function in IRAM using ROM
    ets_io_vprintf with SDK defined serial print drivers. Source in rtos SDK.
    * Why did they not use the ROM "serial print drivers"? Is there a problem
      with them? igrr also didn't use them in ...postmortem.
  * ets_vprintf - by it self is safe.
  * newlibc printf - not safe - lives in flash.
  * newlibc snprintf - not safe - lives in flash.

  Research TODO, Unknowns:
  * ets_printf_plus - is it safe?
    * check if it uses alloc?
    * confirmed it is in IRAM in SDK!
  * Is there a problem with ROM "serial print drivers"?
    Rtos SDK does not use them. igrr also didn't use them in ...postmortem.
    
 */

#include <stdio.h>
#include <string.h>
#include <pgmspace.h>
#include <core_esp8266_features.h>

extern "C" {

#if 1


int _isr_safe_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Note, _isr_safe_printf_P will not handle additional string arguments in
// PROGMEM. Only the 1st parameter, fmt, is supported in PROGMEM.
#define ISR_PRINTF(fmt, ...) _isr_safe_printf_P(PSTR(fmt), ##__VA_ARGS__)
#define ISR_PRINTF_P(fmt, ...) _isr_safe_printf_P(fmt, ##__VA_ARGS__)


// ROM _putc1, ignores CRs and sends CR/LF for LF, newline.
// Always returns character sent.
int constexpr (*_rom_putc1)(int) = (int (*)(int))0x40001dcc;
void uart_buff_switch(uint8_t);

int _isr_safe_printf_P(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int ICACHE_RAM_ATTR _isr_safe_printf_P(const char *fmt, ...) {
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
    /*
      To use ets_strlen() and ets_memcpy() safely with PROGMEM, flash storage,
      the PROGMEM address must be word (4 bytes) aligned. The destination
      address for ets_memcpy must also be word-aligned. We also round the
      buf_len up to the nearest word boundary. So that all transfers will be
      whole words.
    */
    size_t str_len = ets_strlen(fmt);
    size_t buf_len = (str_len + 1 + 3) & ~0x03U;
    char ram_buf[buf_len] __attribute__ ((aligned(4)));
    ets_memcpy(ram_buf, fmt, buf_len);
    va_list argPtr;
    va_start(argPtr, fmt);
    int result = ets_vprintf(_rom_putc1, ram_buf, argPtr);
    va_end(argPtr);
    return result;
}
#endif

#if 0
// Alternate print driver

#ifdef DEBUG_ESP_PORT
#define VALUE(x) __STRINGIFY(x)

void ICACHE_RAM_ATTR uart_write_char_d(char c) {
    // Preprocessor and compiler together will optimize away the if.
    if (strcmp("Serial", VALUE(DEBUG_ESP_PORT)) == 0) {
        // USS - get uart{0,1} status word
        // USTXC - bit offset to TX FIFO count, 8 bit field
        // USF - Uart FIFO
        // Wait for space for two or more characters.
        while (((USS(0) >> USTXC) & 0xff) >= 0x7e) { }

        if (c == '\n') {
            USF(0) = '\r';
        }
        USF(0) = c;
    } else {
        while (((USS(1) >> USTXC) & 0xff) >= 0x7e) { }

        if (c == '\n') {
            USF(1) = '\r';
        }
        USF(1) = c;
    }
}
#else // ! DEBUG_ESP_PORT
void ICACHE_RAM_ATTR uart_write_char_d(char c) {
    uart0_write_char_d(c);
    uart1_write_char_d(c);
}

void ICACHE_RAM_ATTR uart0_write_char_d(char c) {
    while (((USS(0) >> USTXC) & 0xff)) { }

    if (c == '\n') {
        USF(0) = '\r';
    }
    USF(0) = c;
}

void ICACHE_RAM_ATTR uart1_write_char_d(char c) {
    while (((USS(1) >> USTXC) & 0xff) >= 0x7e) { }

    if (c == '\n') {
        USF(1) = '\r';
    }
    USF(1) = c;
}
#endif

#endif

};




#include <stdio.h>
#include <string.h>
#include <pgmspace.h>
#include <core_esp8266_features.h>

extern "C" {

#if 1
/*
  Printing from the malloc routines is tricky. Since a lot of library calls
  will want to do malloc.

  Objective:  To be able to print "last gasp" diagnostic messages
  when interrupts are disabled and w/o availability of heap resources.
*/

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

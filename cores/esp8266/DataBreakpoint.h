#ifndef DATABREAKPOINT_H
#define DATABREAKPOINT_H

#include "esp8266_undocumented.h"

////////////////////////////////////////////////////////////////////////////////
/*
  inspired by https://gist.github.com/earlephilhower/f9f8fa2420ca03b82395da142ad5fc08

  Data Breakpoint (DBREAK) - only one BP is supported on the ESP8266. It is
  set through special registers DBREAKA (address) and DBREAKC (R/W and mask).

  key points to keep in mind:

  1) Data block address must be aligned based on its size.
     exp. A 16 byte data block (bytes_16) must have a 0xXXXXXXX0 address.

  2) Block size is expressed as a power of 2.
     The block size in bytes, ranges from 1 to 64.

  3) BPs are only processed when INTLEVEL is less than 2! Otherwise, the
     event is silently ignored! For Arduino compatible ISR behavior, all ISRs
     run with INTLEVEL 15, dbreak will not work.
*/
extern "C" {

struct DBREAK_S {
  const void *a = NULL;
  uint32_t c = 0;
};

enum dbreak_block_sz {
  bytes_1  = 0x03F, //0
  bytes_2  = 0x03E, //1
  bytes_4  = 0x03C, //2
  bytes_8  = 0x038, //3
  bytes_16 = 0x030, //4
  bytes_32 = 0x020, //5
  bytes_64 = 0x000, //6
};

#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE inline __attribute__ ((always_inline))
#endif


ALWAYS_INLINE struct DBREAK_S
restoreDataBreakpoint(struct DBREAK_S previous) {
  uint32_t tmp;
  const size_t intlevel = 15u;
  asm volatile(
    "memw\n\t"    // My thinking - ensure that pipeline data that breaks has been processed. I am not sure this is necessary.
    "excw\n\t"
    "rsil           %[old_ps],     %[new_intlevel]\n\t"
    "xsr.dbreaka0   %[addr]\n\t"        // 144 == DBREAKA  // dsync
    "xsr.dbreakc0   %[RW_Mask]\n\t"     // 160 == DBREAKC  // dsync
    "wsr.ps         %[old_ps]\n\t"
    "rsync\n\t"
    : // outputs
      [addr]"+r"(previous.a),
      [RW_Mask]"+r"(previous.c),
      [old_ps]"=&r"(tmp)
    : // inputs
      [new_intlevel]"i"(intlevel)
    :
  );
  return previous;
}


ALWAYS_INLINE struct DBREAK_S
init_dbreak_s(const void *addr,
              const dbreak_block_sz mask,
              const bool read,
              const bool write) {
  struct DBREAK_S dbreak;
  dbreak.c  = (read)  ? 1u << 30 : 0;
  dbreak.c |= (write) ? 1u << 31 : 0;
  dbreak.c |= mask;
  dbreak.a = addr;
  return dbreak;
}


ALWAYS_INLINE struct DBREAK_S
setDataBreakpoint(const void *addr,
                  const dbreak_block_sz mask,
                  const bool read,
                  const bool write) {
  struct DBREAK_S dbreak = init_dbreak_s(addr, mask, read, write);
  return restoreDataBreakpoint(dbreak);
}


/*
  Prerequisite, a prior call to disable_dbreak()
  Argument c is the return value from the call to disable_dbreak().
*/
ALWAYS_INLINE
uint32_t enable_dbreak(uint32_t c) {
  asm volatile(
    "memw\n\t"
    "xsr.dbreakc0 %0\n\t"
    "dsync\n\t"
    : "+r" (c)
    ::
  );
  return c;
}


ALWAYS_INLINE
uint32_t clear_dbreak(void) {
  return enable_dbreak(0);
}


ALWAYS_INLINE
uint32_t soc_get_debugcause(void) {
  uint32_t debugcause;
  asm volatile (
    // "movi %[cause], 0\n\t"
    // "wsr  %[cause], dbreakc0\n\t"
    "rsr.debugcause %[cause]\n\t"
    : [cause]"=r"(debugcause)
    :
    : "memory"
  );

  return debugcause;
}

};

////////////////////////////////////////////////////////////////////////////////
#endif // DATABREAKPOINT_H

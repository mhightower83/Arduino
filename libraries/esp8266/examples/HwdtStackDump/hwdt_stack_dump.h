#ifndef HWDT_STACK_DUMP_H
#define HWDT_STACK_DUMP_H

typedef struct STACK_USAGES {
    uint32_t rom;
    uint32_t sys;
    uint32_t cont;
    uint32_t rtc_sys_reason;
} STACK_USAGES_t;

extern uint32_t *g_rom_stack;
extern STACK_USAGES_t stack_usages;

#endif

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

#include <cont.h>
extern cont_t* g_pcont;

extern uint32_t *g_rom_stack;
extern size_t g_rom_stack_A16_sz;

extern const char _rodata_start[];
extern const char _rodata_end[];
extern const char _data_end[];
extern const char _data_start[];

extern const char _bss_table_start[];
extern const char _bss_end[];
extern const char _bss_start[];


void printDramMap(Print& out) {
  size_t sz = 0x040000000 - 0x03FFE8000;
  out.printf("\nDRAM Start ADDR                   = 0x%08X, length 0x%05X, %6u\n", (size_t)0x03FFE8000, sz, sz);

  sz = (size_t)_data_end - (size_t)_data_start;
  out.printf(  "_data_start                       = 0x%08X, length 0x%05X, %6u\n", (size_t)_data_start, sz, sz);
  out.printf(  "_data_end                         = 0x%08X\n", (size_t)_data_end);
  sz = (size_t)_rodata_start - (size_t)_data_end;
  out.printf(  ".noinit                           = 0x%08X, length 0x%05X, %6u\n", (size_t)_rodata_end, sz, sz);

  sz = (size_t)_rodata_end - (size_t)_rodata_start;
  out.printf(  "RO Constants and BSS              = 0x%08X, length 0x%05X, %6u\n", (size_t)_rodata_start, sz, sz);
  sz = (size_t)_rodata_end - (size_t)_bss_table_start;
  out.printf(  "RO Constants and BSS table start  = 0x%08X, length 0x%05X, %6u\n", (size_t)_bss_table_start, sz, sz);
  out.printf(  "RO Constants and BSS End          = 0x%08X\n", (size_t)_rodata_end);

  sz = (size_t)_bss_end - (size_t)_bss_start;
  out.printf(  "BSS start                         = 0x%08X, length 0x%05X, %6u\n", (size_t)_bss_start, sz, sz);
  sz = sizeof(cont_t);
  out.printf(  "  cont_t *g_pcont                 = 0x%08X, length 0x%05X, %6u\n", (size_t)g_pcont, sz, sz);
  uint32_t cont_stack = (uint32_t) & (g_pcont->stack);
  uint32_t cont_stack_first = (uint32_t) g_pcont->stack_end;
  sz = (size_t)cont_stack_first - cont_stack;
  out.printf(  "    cont stack[]                  = 0x%08X, length 0x%05X, %6u\n", cont_stack, sz, sz);
  out.printf(  "    cont stack first              = 0x%08X\n", cont_stack_first);
  out.printf(  "BSS End                           = 0x%08X\n", (size_t)_bss_end);


  sz = UMM_MALLOC_CFG_HEAP_SIZE;
  out.printf(  "Start of umm_malloc Heap ADDR     = 0x%08X, length 0x%05X, %6u\n", (size_t)UMM_MALLOC_CFG_HEAP_ADDR, sz, sz);
  out.printf(  "End of umm_malloc Heap            = 0x%08X\n", (size_t)0x3fffc000);
  sz = (size_t)0x040000000 - 0x3fffc000;
  out.printf(  "ETS System data RAM               = 0x%08X, length 0x%05X, %6u\n", (size_t)0x3fffc000, sz, sz);  // ETS system data ram
  sz = 0x03fffeb30 - 0x03fffdab0;
  out.printf(  "ROM BSS area starts               = 0x%08X, length 0x%05X, %6u\n", (size_t)0x3fffdab0, sz, sz);
  if (*(size_t *)0x3fffdd30)
    out.printf(  "ROM Heap Start *0x3fffdd30      = 0x%08X\n", *(size_t *)0x3fffdd30 );
  if (*(size_t *)0x3fffdd34)
    out.printf(  "ROM Heap End   *0x3fffdd34      = 0x%08X\n", *(size_t *)0x3fffdd34 );
  if (*(size_t *)0x3fffdd34)
    out.printf(  "ROM Heap ??    *0x3fffdd38      = 0x%08X\n", *(size_t *)0x3fffdd38 );
  out.printf(  "ROM BSS area ends                 = 0x%08X\n", (size_t)0x03fffeb30);

  sz = (uint32_t)g_rom_stack - 0x03fffeb30;
  out.printf(  "sys stack                         = 0x%08X, length 0x%05X, %6u\n", 0x03fffeb30, sz, sz);
  out.printf(  "sys stack first                   = 0x%08X\n", (uint32_t)g_rom_stack);


  sz = 0x40000000 - (uint32_t)g_rom_stack;
  out.printf(  "ROM stack                         = 0x%08X, length 0x%05X, %6u\n", (uint32_t)g_rom_stack, sz, sz);
  out.printf(  "g_rom_stack                       = 0x%08X, length 0x%05X, %6u\n", (uint32_t)g_rom_stack, g_rom_stack_A16_sz, g_rom_stack_A16_sz);
  out.printf(  "ROM stack first                   = 0x%08X\n", 0x40000000);
  out.println();
}

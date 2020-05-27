/*
 * Local Additions/Enhancements
 *
 */
#if defined(BUILD_UMM_MALLOC_C)

#if defined(UMM_CRITICAL_METRICS)
/*
 * umm_malloc performance measurments for critical sections
 */
UMM_TIME_STATS time_stats = {
    {0xFFFFFFFF, 0U, 0U, 0U},
    {0xFFFFFFFF, 0U, 0U, 0U},
    {0xFFFFFFFF, 0U, 0U, 0U},
#ifdef UMM_INFO
    {0xFFFFFFFF, 0U, 0U, 0U},
#endif
#ifdef UMM_POISON_CHECK
    {0xFFFFFFFF, 0U, 0U, 0U},
#endif
#ifdef UMM_INTEGRITY_CHECK
    {0xFFFFFFFF, 0U, 0U, 0U},
#endif
    {0xFFFFFFFF, 0U, 0U, 0U} };

bool ICACHE_FLASH_ATTR get_umm_get_perf_data(UMM_TIME_STATS *p, size_t size)
{
    UMM_CRITICAL_DECL(id_no_tag);
    if (p && sizeof(time_stats) == size)
    {
        UMM_CRITICAL_ENTRY(id_no_tag);
        memcpy(p, &time_stats, size);
        UMM_CRITICAL_EXIT(id_no_tag);
        return true;
    }
    return false;
}
#endif

// Alternate Poison functions

#if defined(UMM_POISON_CHECK_LITE)
// We skip this when doing the full poison check.

static int check_poison_neighbors( unsigned short cur ) {
  unsigned short int c;

  if ( 0 == cur )
    return 1;

  c = UMM_PBLOCK(cur) & UMM_BLOCKNO_MASK;
  while( c && (UMM_NBLOCK(c) & UMM_BLOCKNO_MASK) ) {
    /*
       There can be up to 1 free block neighbor in either direction.
       This loop should self limit to 2 passes, due to heap design.
       i.e. Adjacent free space is always consolidated.
     */
    if ( !(UMM_NBLOCK(c) & UMM_FREELIST_MASK) ) {
      if ( !check_poison_block(&UMM_BLOCK(c)) )
        return 0;

      break;
    }

    c = UMM_PBLOCK(c) & UMM_BLOCKNO_MASK;
  }

  c = UMM_NBLOCK(cur) & UMM_BLOCKNO_MASK;
  while( (UMM_NBLOCK(c) & UMM_BLOCKNO_MASK) ) {
    if ( !(UMM_NBLOCK(c) & UMM_FREELIST_MASK) ) {
      if ( !check_poison_block(&UMM_BLOCK(c)) )
        return 0;

      break;
    }

    c = UMM_NBLOCK(c) & UMM_BLOCKNO_MASK;
  }

  return 1;
}
#endif

#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)

/* ------------------------------------------------------------------------ */

static void *get_unpoisoned_check_neighbors( void *v_ptr, const char* file, int line ) {
  unsigned char *ptr = (unsigned char *)v_ptr;

  if (ptr != NULL) {

    ptr -= (sizeof(UMM_POISONED_BLOCK_LEN_TYPE) + UMM_POISON_SIZE_BEFORE);

#if defined(UMM_POISON_CHECK_LITE)
    UMM_CRITICAL_DECL(id_poison);
    unsigned short int c;
    bool poison = false;

    /* Figure out which block we're in. Note the use of truncated division... */
    c = (((char *)ptr)-(char *)(&(umm_heap[0])))/sizeof(umm_block);

    UMM_CRITICAL_ENTRY(id_poison);
    poison = check_poison_block(&UMM_BLOCK(c)) && check_poison_neighbors(c);
    UMM_CRITICAL_EXIT(id_poison);

    if (!poison) {
      if (file) {
        __panic_func(file, line, "");
      } else {
        abort();
      }
    }
#else
    /*
     *  No need to check poison here. POISON_CHECK() has already done a
     *  full heap check.
     */
    (void)file;
    (void)line;
#endif
  }

  return (void *)ptr;
}

/* ------------------------------------------------------------------------ */

void *umm_poison_realloc_fl(void *ptr, size_t size, const char* file, int line) {
    void *ret;

    ptr = get_unpoisoned_check_neighbors(ptr, file, line);

    size += poison_size(size);
    ret = umm_realloc(ptr, size);

    ret = get_poisoned(ret, size);

    return ret;
}

/* ------------------------------------------------------------------------ */

void umm_poison_free_fl(void *ptr, const char* file, int line) {

    ptr = get_unpoisoned_check_neighbors(ptr, file, line);

    umm_free(ptr);
}
#endif

/* ------------------------------------------------------------------------ */

#if defined(UMM_STATS) || defined(UMM_STATS_FULL) || defined(UMM_INFO)
size_t umm_block_size( void ) {
  return sizeof(umm_block);
}
#endif

#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
/*
Overhead adjustments needed for free_blocks to express the number of bytes
that can actually be allocated.
*/
#define UMM_OVERHEAD_ADJUST (\
umm_block_size()/2 + \
UMM_POISON_SIZE_BEFORE + \
UMM_POISON_SIZE_AFTER + \
sizeof(UMM_POISONED_BLOCK_LEN_TYPE))

#else
#define UMM_OVERHEAD_ADJUST  (umm_block_size()/2)
#endif

size_t umm_get_alloc_overhead(void) {
  return (size_t)UMM_OVERHEAD_ADJUST;
}

#if defined(UMM_STATS) || defined(UMM_STATS_FULL)
UMM_STATISTICS ummStats;

static size_t free_blocks_to_free_space(unsigned short int blocks) {
  int free_space = (int)blocks * sizeof(umm_block) - UMM_OVERHEAD_ADJUST;
  /*
   * There are some strange boundary things at play I don't quite follow.
   * However, these adjustments allow malloc to be called and succeed in
   * allocating all of the available memory, assuming it is contiguous.
   */
  return  (free_space > 0) ? (size_t)free_space : 0;
}

// Keep complete call path in IRAM
size_t umm_free_heap_size_lw( void ) {
  if (umm_heap == NULL) {
    umm_init();
  }
#ifdef UMM_INLINE_METRICS
  return free_blocks_to_free_space(ummHeapInfo.freeBlocks);
#else
  return free_blocks_to_free_space(ummStats.free_blocks);
#endif
}
#endif

/*
  I assume xPortGetFreeHeapSize needs to be in IRAM. Since
  system_get_free_heap_size is in IRAM. Which would mean, umm_free_heap_size()
  in flash, was not a safe alternative for returning the same information.
*/
#if defined(UMM_STATS) || defined(UMM_STATS_FULL)
size_t xPortGetFreeHeapSize(void) __attribute__ ((alias("umm_free_heap_size_lw")));
#elif defined(UMM_INFO)
#warning "No ISR safe function available to implement xPortGetFreeHeapSize()"
size_t xPortGetFreeHeapSize(void) __attribute__ ((alias("umm_free_heap_size")));
#endif

#if defined(UMM_STATS) || defined(UMM_STATS_FULL)
void print_stats(int force) {
  DBGLOG_FORCE( force, "umm heap statistics:\n");
#ifdef UMM_INLINE_METRICS
  DBGLOG_FORCE( force,   "  Free Space        %5u\n", ummHeapInfo.freeBlocks * sizeof(umm_block));
#else
  DBGLOG_FORCE( force,   "  Free Space        %5u\n", ummStats.free_blocks * sizeof(umm_block));
#endif
  DBGLOG_FORCE( force,   "  OOM Count         %5u\n", ummStats.oom_count);
#if defined(UMM_STATS_FULL)
  DBGLOG_FORCE( force,   "  Low Watermark     %5u\n", ummStats.free_blocks_min * sizeof(umm_block));
  DBGLOG_FORCE( force,   "  Low Watermark ISR %5u\n", ummStats.free_blocks_isr_min * sizeof(umm_block));
  DBGLOG_FORCE( force,   "  MAX Alloc Request %5u\n", ummStats.alloc_max_size);
#endif
  DBGLOG_FORCE( force,   "  Size of umm_block %5u\n", sizeof(umm_block));
  DBGLOG_FORCE( force, "+--------------------------------------------------------------+\n" );
}
#endif

#if !defined(UMM_INLINE_METRICS) && (defined(UMM_STATS) || defined(UMM_STATS_FULL))
static void umm_fragmentation_metric_init( void ) {
    ummStats.free_blocks = UMM_NUMBLOCKS - 2;
}

static void umm_fragmentation_metric_add( uint16_t c ) {
    uint16_t blocks = (UMM_NBLOCK(c) & UMM_BLOCKNO_MASK) - c;
    DBGLOG_DEBUG( "Add block %d size %d to free metric\n", c, blocks);
    ummStats.free_blocks += blocks;
}

static void umm_fragmentation_metric_remove( uint16_t c ) {
    uint16_t blocks = (UMM_NBLOCK(c) & UMM_BLOCKNO_MASK) - c;
    DBGLOG_DEBUG( "Remove block %d size %d from free metric\n", c, blocks);
    ummStats.free_blocks -= blocks;
}
#endif // !defined(UMM_INLINE_METRICS) && (defined(UMM_STATS) || defined(UMM_STATS_FULL))


int ICACHE_FLASH_ATTR umm_info_safe_printf_P(const char *fmt, ...) {
    /*
      To use ets_strlen() and ets_strcpy() safely with PROGMEM, flash storage,
      the PROGMEM address must be word (4 bytes) aligned. The destination
      address for ets_memcpy must also be word-aligned.
    */
    char ram_buf[ets_strlen(fmt)] __attribute__ ((aligned(4)));
    ets_strcpy(ram_buf, fmt);
    va_list argPtr;
    va_start(argPtr, fmt);
    int result = ets_vprintf(ets_uart_putc1, ram_buf, argPtr);
    va_end(argPtr);
    return result;
}

#endif // BUILD_UMM_MALLOC_C

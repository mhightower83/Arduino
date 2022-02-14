/*
 * Local Additions/Enhancements
 *
 */
#if defined(BUILD_UMM_MALLOC_C)

#if defined(UMM_CRITICAL_METRICS)
/*
 * umm_malloc performance measurements for critical sections
 */
UMM_TIME_STATS time_stats = {
    {0xFFFFFFFF, 0U, 0U, 0U},
    {0xFFFFFFFF, 0U, 0U, 0U},
    {0xFFFFFFFF, 0U, 0U, 0U},
    #ifdef UMM_INFO
    {0xFFFFFFFF, 0U, 0U, 0U},
    #endif
    #if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
    {0xFFFFFFFF, 0U, 0U, 0U},
    #endif
    #ifdef UMM_INTEGRITY_CHECK
    {0xFFFFFFFF, 0U, 0U, 0U},
    #endif
    #ifdef UMM_ENABLE_CHECK_WRAPPERS
    {0xFFFFFFFF, 0U, 0U, 0U},
    #endif
    {0xFFFFFFFF, 0U, 0U, 0U}
};

bool ICACHE_FLASH_ATTR get_umm_get_perf_data(UMM_TIME_STATS *p, size_t size) {
    UMM_CRITICAL_DECL(id_no_tag);
    if (p && sizeof(time_stats) == size) {
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

static bool check_poison_neighbors(
    const umm_heap_context_t * const _context,
    const uint16_t cur) {

    uint16_t c;

    if (0 == cur) {
        return true;
    }

    c = UMM_PBLOCK(cur) & UMM_BLOCKNO_MASK;
    while (c && (UMM_NBLOCK(c) & UMM_BLOCKNO_MASK)) {
        /*
           There can be up to 1 free block neighbor in either direction.
           This loop should self limit to 2 passes, due to heap design.
           i.e. Adjacent free space is always consolidated.
         */
        if (!(UMM_NBLOCK(c) & UMM_FREELIST_MASK)) {
            if (!check_poison_block(&UMM_BLOCK(c))) {
                return false;
            }

            break;
        }

        c = UMM_PBLOCK(c) & UMM_BLOCKNO_MASK;
    }

    c = UMM_NBLOCK(cur) & UMM_BLOCKNO_MASK;
    while ((UMM_NBLOCK(c) & UMM_BLOCKNO_MASK)) {
        if (!(UMM_NBLOCK(c) & UMM_FREELIST_MASK)) {
            if (!check_poison_block(&UMM_BLOCK(c))) {
                return false;
            }

            break;
        }

        c = UMM_NBLOCK(c) & UMM_BLOCKNO_MASK;
    }

    return true;
}
#endif

#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
static bool get_unpoisoned_check_neighbors_core(
    const umm_heap_context_t *const _context,
    struct UMM_PTR_CHECK_RESULTS* ptr_chk,
    const void* const vptr) {

    uintptr_t ptr = (uintptr_t)vptr;

    /* Figure out which block we're in. Note the use of truncated division... */
    // uint16_t c = (ptr - (uintptr_t)(&(_context->pheap[0]))) / sizeof(umm_block);
    uint16_t c = (ptr - (uintptr_t)_context->pheap) / sizeof(umm_block);

    bool poison = check_poison_block(&UMM_BLOCK(c)) && check_poison_neighbors(_context, c);

    if (!poison) {
        ptr_chk->rc = PTR_CHECK_RC::poison;
        ptr_chk->notify = PSTR("Heap panic: Poison check");
    }
    return poison;
}

static void *get_unpoisoned_check_neighbors(const void * const vptr, const void* const caller,  const char *file, const int line) {
    uintptr_t ptr = (uintptr_t)vptr;

    if (ptr != 0) {
        // No return on fail
        umm_check_wrapper(vptr, caller, file, line, get_unpoisoned_check_neighbors_core);

        ptr -= UMM_POISON_SIZE_BEFORE;

        #if (UMM_POINTER_CHECK >= 2)
        /*
         * We save the caller in the UMM_POISON_SIZE_BEFORE field (4 byte
         * field). If the buffer is replaced or freed it will be tagged.
         * Otherwise get_poison will overwrite the field with poison.
         * There is a chance it may get overwriten when freed by future nearby
         * heap allocations.
         */
        *(const void**)ptr = caller;
        #endif

        ptr -= sizeof(UMM_POISONED_BLOCK_LEN_TYPE);
    }

    return (void *)ptr;
}

/* ------------------------------------------------------------------------ */

void umm_poison_free_cfl(void *ptr, const void* const caller, const char *file, int line) {

    ptr = get_unpoisoned_check_neighbors(ptr, caller, file, line);

    umm_free(ptr);
}

/* ------------------------------------------------------------------------ */

void *umm_poison_realloc_cfl(void *ptr, size_t size, const void* const caller, const char *file, int line) {
    void *ret;

    ptr = get_unpoisoned_check_neighbors(ptr, caller, file, line);

    add_poison_size(&size);
    ret = umm_realloc(ptr, size);

    ret = get_poisoned(ret, size);

    return ret;
}
#endif


/* ------------------------------------------------------------------------ */

size_t ICACHE_FLASH_ATTR umm_num_blocks(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return (size_t)UMM_NUMBLOCKS;
}

size_t ICACHE_FLASH_ATTR umm_heap_id(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return (size_t)_context->id;
}

#ifdef UMM_INFO
// Supplemental to umm_info.c

//C Upstream umm_max_block_size() was changed to umm_max_free_block_size().
//C for now setup alias for old function name and deprecate.
size_t umm_max_block_size(void) __attribute__ ((alias("umm_max_free_block_size")));

// needed by Esp-frag.cpp
void ICACHE_FLASH_ATTR umm_get_heap_stats_ctx(umm_heap_context_t *_context, uint32_t* hfree, uint32_t* hmax, uint8_t* hfrag) {
    // L2 / Euclidean norm of free block sizes.
    // Having getFreeHeap()=sum(hole-size), fragmentation is given by
    // 100 * (1 - sqrt(sum(hole-size²)) / sum(hole-size))

    umm_info_ctx(_context, NULL, false);

    uint32_t free_size = _context->info.freeBlocks * UMM_BLOCKSIZE;
    if (hfree) {
        *hfree = free_size;
    }
    if (hmax) {
        *hmax = _context->info.maxFreeContiguousBlocks * UMM_BLOCKSIZE;
    }
    if (hfrag) {
        *hfrag = _context->info.fragmentation_metric;
    }
}

void ICACHE_FLASH_ATTR umm_get_heap_stats(uint32_t* hfree, uint32_t* hmax, uint8_t* hfrag) {
    umm_get_heap_stats_ctx(umm_get_current_heap(), hfree, hmax, hfrag);
}
#endif

#if (UMM_STATS > 0) || defined(UMM_INFO)
size_t umm_block_size(void) {
    #ifdef UMM_VARIABLE_BLOCK_SIZE
    umm_heap_context_t *_context = umm_get_current_heap();
    #endif
    return UMM_BLOCKSIZE;
}
#endif

#if (UMM_STATS > 0)
// Keep complete call path in IRAM
size_t umm_free_heap_size_lw(void) {
    UMM_CHECK_INITIALIZED();

    umm_heap_context_t *_context = umm_get_current_heap();
    return (size_t)_context->UMM_FREE_BLOCKS * UMM_BLOCKSIZE;
}

/*
  I assume xPortGetFreeHeapSize needs to be in IRAM. Since
  system_get_free_heap_size is in IRAM. Which would mean, umm_free_heap_size()
  in flash, was not a safe alternative for returning the same information.
*/
size_t xPortGetFreeHeapSize(void) __attribute__ ((alias("umm_free_heap_size_lw")));
//C Leave this comment for now as a reminder I had this problem.
//C The problem appears to have resolved itself. ???
/*C ???
  I am not sure why this warning just started appearing
  "warning: 'size_t xPortGetFreeHeapSize()' specifies less restrictive attribute
      than its target 'size_t umm_free_heap_size_lw()': 'nothrow' [-Wmissing-attributes]"

  https://gcc.gnu.org/bugzilla/show_bug.cgi?id=81824 looks relavent
  Adding "__attribute__ ((nothrow))" seems to resolve the issue.
 */

#elif defined(UMM_INFO)
#ifndef UMM_INLINE_METRICS
#warning "No ISR safe function available to implement xPortGetFreeHeapSize()"
#endif
size_t xPortGetFreeHeapSize(void) __attribute__ ((alias("umm_free_heap_size")));
#endif

#if (UMM_STATS > 0)
void umm_print_stats(void) {
    bool force = true;
    umm_heap_context_t *_context = umm_get_current_heap();
    DBGLOG_FORCE(force, "umm heap statistics:\n");
    DBGLOG_FORCE(force,   "  Heap ID           %7u\n", _context->id);
    DBGLOG_FORCE(force,   "  Free Space        %7u\n", _context->UMM_FREE_BLOCKS * UMM_BLOCKSIZE);
    DBGLOG_FORCE(force,   "  OOM Count         %7u\n", _context->UMM_OOM_COUNT);

    #if (UMM_STATS > 1)
    DBGLOG_FORCE(force,   "  Low Watermark     %7u\n", _context->stats.free_blocks_min * UMM_BLOCKSIZE);
    #endif
    #if (UMM_STATS >= 10) // UMM_STATS_FULL
    DBGLOG_FORCE(force,   "  Low Watermark ISR %7u\n", _context->stats.free_blocks_isr_min * UMM_BLOCKSIZE);
    DBGLOG_FORCE(force,   "  MAX Alloc Request %7u\n", _context->stats.alloc_max_size);
    #endif
    DBGLOG_FORCE(force,   "  Size of heap      %7u\n", UMM_HEAPSIZE);
    DBGLOG_FORCE(force,   "  Size of umm_block %7u\n", UMM_BLOCKSIZE);
    DBGLOG_FORCE(force,   "  Total numblocks   %7u\n", UMM_NUMBLOCKS);
    DBGLOG_FORCE(force, "+--------------------------------------------------------------+\n");
}
#endif

int ICACHE_FLASH_ATTR umm_info_safe_printf_P(const char *fmt, ...) {
    char ram_buf[strlen_P(fmt) + 1];
    strcpy_P(ram_buf, fmt);
    va_list argPtr;
    va_start(argPtr, fmt);
    int result = ets_vprintf(ets_uart_putc1, ram_buf, argPtr);
    va_end(argPtr);
    return result;
}

#if (UMM_STATS > 0)
size_t ICACHE_FLASH_ATTR umm_get_oom_count(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->UMM_OOM_COUNT;
}
#endif

#if (UMM_STATS > 1)
size_t ICACHE_FLASH_ATTR umm_free_heap_size_min(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.free_blocks_min * umm_block_size();
}

/*
  Having both umm_free_heap_size_lw_min and umm_free_heap_size_min was an
  accident. They contained the same code. Keep umm_free_heap_size_min and alias
  and depricate the other.
*/
size_t umm_free_heap_size_lw_min(void) __attribute__ ((alias("umm_free_heap_size_min")));

size_t ICACHE_FLASH_ATTR umm_free_heap_size_min_reset(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    _context->stats.free_blocks_min = _context->UMM_FREE_BLOCKS;
    return _context->stats.free_blocks_min * umm_block_size();
}
#endif

#if (UMM_STATS >= 10) // UMM_STATS_FULL
size_t ICACHE_FLASH_ATTR umm_free_heap_size_isr_min(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.free_blocks_isr_min * umm_block_size();
}

size_t ICACHE_FLASH_ATTR umm_get_max_alloc_size(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.alloc_max_size;
}

size_t ICACHE_FLASH_ATTR umm_get_last_alloc_size(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.last_alloc_size;
}

size_t ICACHE_FLASH_ATTR umm_get_malloc_count(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.id_malloc_count;
}

size_t ICACHE_FLASH_ATTR umm_get_malloc_zero_count(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.id_malloc_zero_count;
}

size_t ICACHE_FLASH_ATTR umm_get_realloc_count(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.id_realloc_count;
}

size_t ICACHE_FLASH_ATTR umm_get_realloc_zero_count(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.id_realloc_zero_count;
}

size_t ICACHE_FLASH_ATTR umm_get_free_count(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.id_free_count;
}

size_t ICACHE_FLASH_ATTR umm_get_free_null_count(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    return _context->stats.id_free_null_count;
}
#endif // UMM_STATS_FULL

#if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
/*
 * Saturated unsigned add
 * Poison added to allocation size requires overflow protection.
 */
static size_t umm_uadd_sat(const size_t a, const size_t b) {
    size_t r = a + b;
    if (r < a) {
        return SIZE_MAX;
    }
    return r;
}
#endif

/*
 * Use platform-specific functions to protect against unsigned overflow/wrap by
 * implementing saturated unsigned multiply.
 * The function umm_calloc requires a saturated multiply function.
 */
size_t umm_umul_sat(const size_t a, const size_t b) {
    size_t r;
    if (__builtin_mul_overflow(a, b, &r)) {
        return SIZE_MAX;
    }
    return r;
}


///////////////////////////////////////////////////////////////////////////////
//
//
#if (UMM_POINTER_CHECK >= 1) || defined(UMM_ENABLE_CHECK_WRAPPERS)
struct UMM_PTR_CHECK_RESULTS umm_ptr_check_results;
#endif

#if (UMM_POINTER_CHECK == 1) || (UMM_POINTER_CHECK == 2)
///////////////////////////////////////////////////////////////////////////////
// UMM_POINTER_CHECK
//
static bool umm_pointer_quick_check_core(
    const umm_heap_context_t *const _context,
    struct UMM_PTR_CHECK_RESULTS *ptr_chk,
    const void* const vptr) {

    // struct UMM_PTR_CHECK_RESULTS *ptr_chk = &umm_ptr_check_results;

    uintptr_t umm_data_ptr = (uintptr_t)vptr;

    ptr_chk->rc = good;

    // Do some simple tests to catch consecutive frees.
    do {
        if (NULL == _context) {
            ptr_chk->rc = corrupt;
            continue;
        }

        uint16_t cur = (umm_data_ptr - (uintptr_t)_context->pheap) / sizeof(umm_block);

        /* Range check block number */
        if (cur >= UMM_NUMBLOCKS || 0 == cur) {
            ptr_chk->rc = bad;
            continue;
        }

        /*
         * The value returned by UMM_NBLOCK(cur) is the block number of the next
         * umm_block. A high bit set (UMM_FREELIST_MASK) indicates a free block.
         * UMM_NBLOCK(cur) and-ed with UMM_BLOCKNO_MASK gives the block number.
         */
        uint16_t next = UMM_NBLOCK(cur);

        /*
         * The high bit should be off for our pointer.
         *
         * In the non-debug case for umm_free(), when umm_assimilate_down()
         * occurs, the UMM_FREELIST_MASK bit is not set because the freed
         * allocation is in the higher part of a free block. To assist in
         * "double-free detection," optional code is added to apply the
         * UMM_FREELIST_MASK bit.
         */
        if (next >= UMM_NUMBLOCKS) { // implicitly catch when UMM_FREELIST_MASK is set
            if ((next & UMM_BLOCKNO_MASK) >= UMM_NUMBLOCKS) {
                ptr_chk->rc = bad;
            } else {
                ptr_chk->rc = free_mark; // This may be a double free.
            }
            continue;
        }

        /*
         * Now we know UMM_FREELIST_MASK is not set
         * umm_blocks are linked in order. UMM_NBLOCK() should return a value
         * higher than cur provide the UMM_FREELIST_MASK bit is clear.
         */
        if (cur >= next) {
            ptr_chk->rc = bad;
            continue;
        }

        /* Check for near umm_data value, but not accurate */
        if ((uintptr_t)&UMM_DATA(cur) != umm_data_ptr) {
            /*
             * The address is close but suspiciously doesn't match.
             * Internal to umm_malloc this would work; however, it should not
             * occur with an address from outside umm_malloc.
             */
            ptr_chk->rc = accuracy;
            continue;
        }

#if (UMM_POINTER_CHECK >= 2)
        /* Import more test from Integrity check */

        uint16_t prev = UMM_PBLOCK(cur);

        /* Check that next block number is valid */
        if (prev >= UMM_NUMBLOCKS) {
            ptr_chk->rc = bad;
            continue;
        }

        /* make sure the block list is sequential */
        if (next <= cur || cur <= prev) {
            ptr_chk->rc = bad;
            continue;
        }

        /* Check if prev block number points back to cur */
        if (UMM_PBLOCK(next) != cur) {
            ptr_chk->rc = bad;
            continue;
        }

        /* Check if next block number points back to cur
         * Note, the and-ing with UMM_BLOCKNO_MASK.
         * The previous block may be free.
         */
        if ((UMM_NBLOCK(prev) & UMM_BLOCKNO_MASK) != cur) {
            ptr_chk->rc = bad;
            continue;
        }

        // An enhancement for later, maybe, would be to probe NFREE, PFREE of prev and next
#endif
    } while(false);

    if (good == ptr_chk->rc) {
        return true;
    }
    ptr_chk->notify = PSTR("Heap panic: free/realloc");
    return false;
}
#endif

#if (UMM_POINTER_CHECK == 3)
///////////////////////////////////////////////////////////////////////////////
// UMM_POINTER_CHECK by finding pointer in the heap
//
/*
 * Verify an allocation is in the heap by a heap allocated pointer value that
 * has been adjusted to point back at the umm_block address.
 *
 * This logic is based on umm_info find ptr option. To improve speed and size,
 * all other heap metric computations and printing has been removed.
 * For IRQ context protections, must be called from UMM_CRITICAL wrapper.
 *
 * To discover which allocation may contain the address, look at within_blockNo
 */
static bool umm_pointer_full_check_core(
      const umm_heap_context_t *const _context,
      struct UMM_PTR_CHECK_RESULTS *ptr_chk,
      const void* const vptr) {
      // struct UMM_PTR_CHECK_RESULTS *ptr_chk = &umm_ptr_check_results;

    ptr_chk->rc = good;
    ptr_chk->near_ptr = 0u;
    ptr_chk->near_next = 0u;

    if (NULL == _context) {
        ptr_chk->rc = corrupt;
        ptr_chk->notify = PSTR("Heap panic: free/realloc");
        return false;
    }

    // const void * const ptr = (void *)((uintptr_t)vptr  - (uintptr_t)(&(_context->pheap[0])));
    const void * const ptr = (void *)((uintptr_t)vptr  - (uintptr_t)_context->pheap);

    /*
     * Now loop through the block lists. The terminating condition is an nb
     * pointer with a value of zero...
     */
    uint16_t nextBlockNo = 0, blockNo = UMM_NBLOCK(0) & UMM_BLOCKNO_MASK;
    for (; (UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK); blockNo = nextBlockNo) {

        nextBlockNo = UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK;

        /* Is this block free? */
        if ((UMM_NBLOCK(blockNo) & UMM_FREELIST_MASK)) {
            /*
             * Check if ptr is in free space
             */
            if (ptr >= &UMM_BLOCK(blockNo) && ptr < &UMM_BLOCK(nextBlockNo)) {
                ptr_chk->rc = free_space;
                break;
            }
        } else {
            /*
             * Does this block address match the search ptr?
             */
            if (ptr == &UMM_BLOCK(blockNo)) {
                ptr_chk->rc = good;
                break;
            } else
            /*
             * Is the search pointer inside this block?
             */
            if (ptr > &UMM_BLOCK(blockNo) && ptr < &UMM_BLOCK(nextBlockNo)) {
                ptr_chk->rc = inside;
                break;
            }
        }
    }
    if (corrupt != ptr_chk->rc) {
        ptr_chk->near_ptr  = (uintptr_t)&UMM_BLOCK(blockNo);
        ptr_chk->near_next = (uintptr_t)&UMM_BLOCK(nextBlockNo);
    }

    if (good == ptr_chk->rc) {
        return true;
    }
    ptr_chk->notify = PSTR("Heap panic: free/realloc");
    return false;
}
#endif

#if defined(UMM_ENABLE_CHECK_WRAPPERS)
// Used by (UMM_POINTER_CHECK >= 2) || defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_INTEGRITY_CHECK)
/*
 * Takes in sketch ptr
 * sets sketch_ptr element in ptr_chk
 * corrects for poison offset
 * Returns umm_data pointer
 */
static const void* sketch_ptr__add_to_check_results(struct UMM_PTR_CHECK_RESULTS* ptr_chk, const void* const sketch_ptr) {
    uintptr_t umm_data_ptr = ptr_chk->sketch_ptr = (uintptr_t)sketch_ptr;

    #if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
    // called with poisoned sketch pointer
    umm_data_ptr -= sizeof(UMM_POISONED_BLOCK_LEN_TYPE) + UMM_POISON_SIZE_BEFORE;
    #endif

    ptr_chk->notify = NULL;
    ptr_chk->rc = good;

    return (const void*)umm_data_ptr;
}

/*
 * General wrapper for UMM Check functions - allways called from outside umm_malloc
 *
 * Consoladates use of critical section for check functions.
 * Captures caller context info for possible postmortem reporting
 */
void umm_check_wrapper(
    const void* const sketch_ptr,
    const void* const caller,
    const char* const file,
    const int line,
    bool (*fn_check)(const umm_heap_context_t* const, struct UMM_PTR_CHECK_RESULTS*, const void* const)) {

    umm_heap_context_t *_context;
    const void* umm_data_ptr = NULL;

    UMM_CRITICAL_DECL(id_check);
    UMM_CRITICAL_ENTRY(id_check);

    struct UMM_PTR_CHECK_RESULTS* ptr_chk = &umm_ptr_check_results;

    // Fill everything in, in case something fails
    ptr_chk->caller = caller;
    ptr_chk->file = file;
    ptr_chk->line = line;

    if (sketch_ptr) {
        umm_data_ptr = sketch_ptr__add_to_check_results(ptr_chk, sketch_ptr);
        _context = _umm_get_ptr_context(umm_data_ptr);
    } else {
        ptr_chk->sketch_ptr = 0u;
        _context = umm_get_current_heap();
    }

    if (!fn_check(_context, ptr_chk, umm_data_ptr)) {
        // Intentionally call abort with interrupts off (critical section)
        // prevents an IRQ/ISR from creating a 2nd crash while processing the
        // current.
        abort();
    }

    UMM_CRITICAL_EXIT(id_check);
    // return true;
}
#endif

/*
 * These are called from heap.cpp thick wrappers
 *
 * Examples:
 *   umm_pointer_check_wrap(__builtin_return_address(0), ptr, file, line);
 *   umm_poison_check_wrap(__builtin_return_address(0), ptr, file, line);
 *   umm_integrity_check_wrap(__builtin_return_address(0), ptr, file, line);
 */
#if (UMM_POINTER_CHECK == 2)
void umm_pointer_check_wrap(const void* const ptr, const void* const caller, const char* const file, const int line) {
    if (ptr) {
        umm_check_wrapper(ptr, caller, file, line, umm_pointer_quick_check_core);
    }
}
#endif

#if (UMM_POINTER_CHECK == 3)
void umm_pointer_check_wrap(const void* const ptr, const void* const caller, const char* const file, const int line) {
    if (ptr) {
        umm_check_wrapper(ptr, caller, file, line, umm_pointer_full_check_core);
    }
}
#endif

#if defined(UMM_POISON_CHECK)

static bool _umm_poison_check_core(
    const umm_heap_context_t *const _context,
    struct UMM_PTR_CHECK_RESULTS *ptr_chk,
    const void* const vptr) {

    if (_context && !umm_poison_check_core(_context, vptr)) {
        ptr_chk->rc = poison;
        ptr_chk->notify = PSTR("Heap panic: Poison check");
        return false;
    }
    return true;
}

void umm_poison_check_wrap(const void* const ptr, const void* const caller, const char* const file, const int line) {
    return umm_check_wrapper(ptr, caller, file, line, _umm_poison_check_core);
}
#endif

#if defined(UMM_INTEGRITY_CHECK)

static bool _umm_integrity_check_core(
    const umm_heap_context_t *const _context,
    struct UMM_PTR_CHECK_RESULTS *ptr_chk,
    const void* const vptr) {

    if (_context && !umm_poison_check_core(_context)) {
        ptr_chk->rc = integrity;
        ptr_chk->notify = PSTR("Heap panic: Integrity check");
        return false;
    }
    return true;
}

void umm_integrity_check_wrap(const void* const ptr, const void* const caller, const char* const file, const int line) {
    return umm_check_wrapper(ptr, caller, file, line, _umm_integrity_check_core);
}
#endif



#if (UMM_POINTER_CHECK == 1)
/*
 * Takes in umm_data ptr
 * sets sketch_ptr element in ptr_chk
 * corrects for poison offset
 * Returns umm_data pointer
 */
static const void* umm_data_ptr__add_to_check_results(struct UMM_PTR_CHECK_RESULTS* ptr_chk, const void* const umm_data) {
    ptr_chk->sketch_ptr = (uintptr_t)umm_data;
    #if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
    // called with umm_data pointer
    ptr_chk->sketch_ptr += sizeof(UMM_POISONED_BLOCK_LEN_TYPE) + UMM_POISON_SIZE_BEFORE;
    #endif

    ptr_chk->notify = NULL;
    ptr_chk->rc = good;
    // ptr_chk->caller // For UMM_POINTER_CHECK == 1 only, this is set by umm_free or umm_realloc

    return umm_data;
}

// pointer_integrity_check
/*
 * Succeeds or aborts - only called from within umm_malloc by umm_get_ptr_context
 */
static umm_heap_context_t* umm_data_ptr_integrity_check(const void* const umm_data_ptr) {
    umm_heap_context_t *_context;

    UMM_CRITICAL_DECL(id_check);  // Critical section total cost 12 bytes
    UMM_CRITICAL_ENTRY(id_check);

    struct UMM_PTR_CHECK_RESULTS* ptr_chk = &umm_ptr_check_results;

    umm_data_ptr__add_to_check_results(ptr_chk, umm_data_ptr);
    _context = _umm_get_ptr_context(umm_data_ptr);

    if (!umm_pointer_quick_check_core(_context, ptr_chk, umm_data_ptr)) {
        abort();
    }

    UMM_CRITICAL_EXIT(id_check);
    return _context;
}
#endif

#if (UMM_POINTER_CHECK != 0)
void exception_decoder_helper(const void* caller, uintptr_t ptr, void (*fn_printf)(const char *fmt, ...)) {
    if (!caller) {
        return;
    }

    #if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
    void* prev_caller = *(void**)(ptr - UMM_POISON_SIZE_BEFORE);
    // 1st caller to free
    fn_printf(PSTR("  epc1=%p, epc2=0x00000000, epc3=0x00000000, excvaddr=%p, depc=0x00000000\r\n"), prev_caller, (void *)ptr);
    #endif

    // 2nd caller to free
    fn_printf(PSTR("  epc1=%p, epc2=0x00000000, epc3=0x00000000, excvaddr=%p, depc=0x00000000\r\n"), caller, (void *)ptr);
}

/*
 * Postmortem report - prints an interpretation of UMM_PTR_CHECK_RESULTS.
 */
ICACHE_FLASH_ATTR
void umm_postmortem_report(void (*fn_printf)(const char *fmt, ...)) {
    const struct UMM_PTR_CHECK_RESULTS *const ptr_chk = &umm_ptr_check_results;

    if (ptr_chk->notify) {
        fn_printf(PSTR("\n%S\n"), ptr_chk->notify);
#if (UMM_POINTER_CHECK == 3)
        /*
         * Print more specific details about the ptr
         */
        if (corrupt == ptr_chk->rc) {
           fn_printf(PSTR("  The pointer %p is not a Heap address.\n"), (void *)ptr_chk->sketch_ptr);
        } else
        if (free_space == ptr_chk->rc) {
            size_t sz = ptr_chk->near_next - ptr_chk->near_ptr;
            fn_printf(PSTR("  The pointer %p is in the unallocated Heap, umm_block: %p, size: %u.\n"),
                (void *)ptr_chk->sketch_ptr, (void *)ptr_chk->near_ptr, sz);
        } else
        if (inside == ptr_chk->rc) {
            size_t sz = ptr_chk->near_next - ptr_chk->near_ptr;
            fn_printf(PSTR("  The pointer %p is in umm_block: %p, size: %u.\n"),
                (void *)ptr_chk->sketch_ptr, (void *)ptr_chk->near_ptr, sz);
        }
#else
        if (free_mark == ptr_chk->rc) {
            fn_printf(PSTR("  Pointer %p a double free() or corrupted. Found the free bit set.\n"), (void *)ptr_chk->sketch_ptr);
        } else
        if (corrupt == ptr_chk->rc) {
            fn_printf(PSTR("  The pointer %p is not a Heap address.\n"), (void *)ptr_chk->sketch_ptr);
        } else {
            fn_printf(PSTR("  The pointer %p is not an active allocation.\n"), (void *)ptr_chk->sketch_ptr);
        }
#endif
    } else
    if (ptr_chk->caller) {
        // Nothing is known for certain. Just providing extra info.
        fn_printf(PSTR("\nLast Heap free/realloc\n"));
        fn_printf(PSTR("  Used pointer %p.\n"), (void *)ptr_chk->sketch_ptr);
    } else
    {
        // Silent return on no problem.
        return;
    }

    fn_printf(PSTR("  Last free/realloc caller: %p\n"), ptr_chk->caller);

    #if (UMM_POINTER_CHECK >= 2)
    // defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(DEBUG_ESP_OOM)
    if (ptr_chk->file) {
        fn_printf(PSTR("  File: %S:%d\n"), ptr_chk->file, ptr_chk->line);
    }
    #endif

    exception_decoder_helper(ptr_chk->caller, ptr_chk->sketch_ptr, fn_printf);

}
#endif

///////////////////////////////////////////////////////////////////////////////


#endif // BUILD_UMM_MALLOC_C

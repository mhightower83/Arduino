#if defined(BUILD_UMM_MALLOC_C)

#ifdef UMM_INFO

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <math.h>

#include <ctype.h>
// #include <stdio.h>

/* ----------------------------------------------------------------------------
 * One of the coolest things about this little library is that it's VERY
 * easy to get debug information about the memory heap by simply iterating
 * through all of the memory blocks.
 *
 * As you go through all the blocks, you can check to see if it's a free
 * block by looking at the high order bit of the next block index. You can
 * also see how big the block is by subtracting the next block index from
 * the current block number.
 *
 * The umm_info function does all of that and makes the results available
 * in the ummHeapInfo structure.
 * ----------------------------------------------------------------------------
 */

// UMM_HEAP_INFO ummHeapInfo;

uint32_t sqrt32(uint32_t n);

void ICACHE_FLASH_ATTR compute_usage_metric(umm_heap_context_t *_context);
void ICACHE_FLASH_ATTR compute_fragmentation_metric(umm_heap_context_t *_context);
void* ICACHE_FLASH_ATTR umm_info_ctx(umm_heap_context_t *_context, void *ptr, [[maybe_unused]] bool force_print);
void* ICACHE_FLASH_ATTR umm_info(void *ptr, bool force_print);

#ifndef UMM_INFO_SAMPLE_DATA_MAX
#define UMM_INFO_SAMPLE_DATA_MAX (64u)
#endif

#if defined(UMM_TAG_POISON_CHECK) || defined(UMM_INFO_SAMPLE_DATA)
extern int __umm_info_tag_decode_cb(char extra[], const size_t extra_sz, const void* const alloc_data, size_t alloc_size) {
    const uint32_t* umm_data = (const uint32_t*)alloc_data;
    uintptr_t sketch_ptr = (uintptr_t)alloc_data;
    size_t sample_sz = (UMM_INFO_SAMPLE_DATA < UMM_INFO_SAMPLE_DATA_MAX) ? UMM_INFO_SAMPLE_DATA : UMM_INFO_SAMPLE_DATA_MAX;

    #if defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE) || defined(UMM_TAG_POISON_CHECK)
    sketch_ptr += sizeof(UMM_POISONED_BLOCK_LEN_TYPE) + UMM_POISON_SIZE_BEFORE;
    alloc_size = (UMM_POISONED_BLOCK_LEN_TYPE)umm_data[0] - UMM_OVERHEAD_ADJUST + 4;
    #endif
    if (sample_sz > alloc_size) {
        sample_sz = alloc_size;
    }

    const char* cptr = (const char*)sketch_ptr;
    char cstr[sample_sz + 1];
    memcpy(cstr, cptr, sample_sz);
    for (size_t u = 0; u < sample_sz; u++) {
        if (isprint(cstr[u]) == 0) {
            cstr[u] = '\0';
        }
    }
    cstr[sample_sz] = '\0';

    #if defined(UMM_TAG_POISON_CHECK)
    int sz = UMM_SPRINTF(extra, extra_sz, "0x%08x %5d+12 0x%08x 0x%08x 0x%08x 0x%08x - %s",
        umm_data[1],
        alloc_size,
        umm_data[2], umm_data[3], umm_data[4], umm_data[5], cstr);
    #elif defined(UMM_POISON_CHECK) || defined(UMM_POISON_CHECK_LITE)
    int sz = UMM_SPRINTF(extra, extra_sz, "%5d+12 0x%08x 0x%08x 0x%08x 0x%08x - %s",
        alloc_size,
        umm_data[2], umm_data[3], umm_data[4], umm_data[5], cstr);
    #else
    int sz = UMM_SPRINTF(extra, extra_sz, "%5d 0x%08x 0x%08x 0x%08x 0x%08x - %s",
        alloc_size, umm_data[0], umm_data[1], umm_data[2], umm_data[3], cstr);
    #endif
    return sz;
}
#else
extern int __umm_info_tag_decode_cb(char extra[], const size_t extra_sz, const void* const vptr, size_t size) {
    if (extra && extra_sz) {
        extra[0] = '\0';
    }
    return 0;
}
#endif
extern int umm_info_tag_decode_cb(char extra[], const size_t extra_sz, const void* const vptr, size_t size) __attribute__ ((weak, alias("__umm_info_tag_decode_cb")));

void compute_usage_metric(umm_heap_context_t *_context) {
    if (0 == _context->info.freeBlocks) {
        _context->info.usage_metric = -1;        // No free blocks!
    } else {
        _context->info.usage_metric = (int)((_context->info.usedBlocks * 100) / (_context->info.freeBlocks));
    }
}


void compute_fragmentation_metric(umm_heap_context_t *_context) {
    if (0 == _context->info.freeBlocks) {
        _context->info.fragmentation_metric = 0; // No free blocks ... so no fragmentation either!
    } else {
        _context->info.fragmentation_metric = 100 - (((uint32_t)(sqrt32(_context->info.freeBlocksSquared)) * 100) / (_context->info.freeBlocks));
    }
}

void *umm_info(void *ptr, [[maybe_unused]] bool force_print) {
    return umm_info_ctx(umm_get_current_heap(), ptr, force_print);
}

void *umm_info_ctx(umm_heap_context_t *_context, void *ptr, [[maybe_unused]] bool force_print) {
    uint16_t blockNo = 0;
    #ifdef UMM_INFO_NO_PRINT
    const bool force = false;
    #else
    const bool force = force_print;
    #endif

    UMM_CRITICAL_DECL(id_info);

    UMM_CHECK_INITIALIZED();

    /* Protect the critical section... */
    UMM_CRITICAL_ENTRY(id_info);

    /*
     * Clear out all of the entries in the ummHeapInfo structure before doing
     * any calculations..
     */
    memset(&_context->info, 0, sizeof(_context->info));

    DBGLOG_FORCE(force, "\n");
    DBGLOG_FORCE(force, "+----------+-------+--------+--------+-------+--------+--------+\n");
    DBGLOG_FORCE(force, "|0x%08lx|B %5d|NB %5d|PB %5d|Z %5d|NF %5d|PF %5d|\n",
        DBGLOG_32_BIT_PTR(&UMM_BLOCK(blockNo)),
        blockNo,
        UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
        UMM_PBLOCK(blockNo),
        (UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK) - blockNo,
        UMM_NFREE(blockNo),
        UMM_PFREE(blockNo));

    /*
     * Now loop through the block lists, and keep track of the number and size
     * of used and free blocks. The terminating condition is an nb pointer with
     * a value of zero...
     */

    blockNo = UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK;

    while (UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK) {
        size_t curBlocks = (UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK) - blockNo;

        ++_context->info.totalEntries;
        _context->info.totalBlocks += curBlocks;

        /* Is this a free block? */

        if (UMM_NBLOCK(blockNo) & UMM_FREELIST_MASK) {
            ++_context->info.freeEntries;
            _context->info.freeBlocks += curBlocks;
            _context->info.freeBlocksSquared += (curBlocks * curBlocks);

            if (_context->info.maxFreeContiguousBlocks < curBlocks) {
                _context->info.maxFreeContiguousBlocks = curBlocks;
            }

            DBGLOG_FORCE(force, "|0x%08lx|B %5d|NB %5d|PB %5d|Z %5u|NF %5d|PF %5d|\n",
                DBGLOG_32_BIT_PTR(&UMM_BLOCK(blockNo)),
                blockNo,
                UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
                UMM_PBLOCK(blockNo),
                (uint16_t)curBlocks,
                UMM_NFREE(blockNo),
                UMM_PFREE(blockNo));

            /* Does this block address match the ptr we may be trying to free? */

            if (ptr == &UMM_BLOCK(blockNo)) {

                /* Release the critical section... */
                UMM_CRITICAL_EXIT(id_info);

                return ptr;
            }
        } else {
            ++_context->info.usedEntries;
            _context->info.usedBlocks += curBlocks;
#if defined(UMM_TAG_POISON_CHECK) || defined(UMM_INFO_SAMPLE_DATA)
            size_t alloc_size = (uint16_t)curBlocks * UMM_BLOCKSIZE - 4;
            void* alloc_data = (void*)&UMM_DATA(blockNo);
            int sz = umm_info_tag_decode_cb(NULL, 0, alloc_data, alloc_size);
            char extra[sz + 1];
            umm_info_tag_decode_cb(extra, sizeof(extra), alloc_data, alloc_size);
            DBGLOG_FORCE(force, "|0x%08lx|B %5d|NB %5d|PB %5d|Z %5u|        |        |%s\n",
                DBGLOG_32_BIT_PTR(&UMM_BLOCK(blockNo)),
                blockNo,
                UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
                UMM_PBLOCK(blockNo),
                (uint16_t)curBlocks,
                extra);
#else
            DBGLOG_FORCE(force, "|0x%08lx|B %5d|NB %5d|PB %5d|Z %5u|        |        |\n",
                DBGLOG_32_BIT_PTR(&UMM_BLOCK(blockNo)),
                blockNo,
                UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
                UMM_PBLOCK(blockNo),
                (uint16_t)curBlocks);
#endif
        }

        blockNo = UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK;
    }

    /*
     * The very last block is used as a placeholder to indicate that
     * there are no more blocks in the heap, so it cannot be used
     * for anything - at the same time, the size of this block must
     * ALWAYS be exactly 1 !
     */

    DBGLOG_FORCE(force, "|0x%08lx|B %5d|NB %5d|PB %5d|Z %5d|NF %5d|PF %5d|\n",
        DBGLOG_32_BIT_PTR(&UMM_BLOCK(blockNo)),
        blockNo,
        UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
        UMM_PBLOCK(blockNo),
        UMM_NUMBLOCKS - blockNo,
        UMM_NFREE(blockNo),
        UMM_PFREE(blockNo));

    DBGLOG_FORCE(force, "+----------+-------+--------+--------+-------+--------+--------+\n");

    DBGLOG_FORCE(force, "Total Entries %5d    Used Entries %5d    Free Entries %5d\n",
        _context->info.totalEntries,
        _context->info.usedEntries,
        _context->info.freeEntries);

    DBGLOG_FORCE(force, "Total Blocks  %5d    Used Blocks  %5d    Free Blocks  %5d\n",
        _context->info.totalBlocks,
        _context->info.usedBlocks,
        _context->info.freeBlocks);

    DBGLOG_FORCE(force, "+--------------------------------------------------------------+\n");

    compute_usage_metric(_context);
    DBGLOG_FORCE(force, "Usage Metric:               %5d\n", _context->info.usage_metric);

    compute_fragmentation_metric(_context);
    DBGLOG_FORCE(force, "Fragmentation Metric:       %5d\n", _context->info.fragmentation_metric);

    DBGLOG_FORCE(force, "+--------------------------------------------------------------+\n");

    #if defined(UMM_DEV_DEBUG) && (UMM_STATS > 0)
    // This is really a umm_malloc maintenace thing. Keep this out of the user build.
    #if !defined(UMM_INLINE_METRICS)
    if (_context->info.freeBlocks == _context->stats.free_blocks) {
        DBGLOG_FORCE(force, "heap info Free blocks and heap statistics Free blocks match.\n");
    } else {
        DBGLOG_FORCE(force, "\nheap info Free blocks  %5d != heap statistics Free Blocks  %5d\n\n",
            _context->info.freeBlocks,
            _context->stats.free_blocks);
    }
    DBGLOG_FORCE(force, "+--------------------------------------------------------------+\n");
    #endif
    #endif

    if (force) {
        umm_print_stats();
    }

    /* Release the critical section... */
    UMM_CRITICAL_EXIT(id_info);

    #ifdef UMM_INFO_NO_PRINT
    // For a memory tight build no printing from umm_info.
    // If umm_print_stats() report is needed it can be called directly.
    // If never called it doesn't take any FLASH space.
    // If debug build, let them know why we are not printing.
    // Otherwise lets keep FLASH usage down.
    #if defined(DEBUG_ESP_PORT) || defined(DEBUG_ESP_CORE)
    DBGLOG_FORCE(force_print, "\n*** umm_info() cannot print. Remove build option '-DUMM_INFO_NO_PRINT'.\n")
    #endif
    #endif

    return NULL;
}

/* ------------------------------------------------------------------------ */

size_t umm_free_heap_size(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    #ifndef UMM_INLINE_METRICS
    umm_info_ctx(_context, NULL, false);
    #endif
    return (size_t)_context->info.freeBlocks * UMM_BLOCKSIZE;
}

size_t umm_max_free_block_size(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    umm_info_ctx(_context, NULL, false);
    return _context->info.maxFreeContiguousBlocks * UMM_BLOCKSIZE;
}

int umm_usage_metric(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    #ifdef UMM_INLINE_METRICS
    compute_usage_metric(_context);
    #else
    umm_info_ctx(_context, NULL, false);
    #endif
    DBGLOG_DEBUG("usedBlocks %i totalBlocks %i\n", _context->info.usedBlocks, _context->info.totalBlocks);

    return _context->info.usage_metric;
}

int umm_fragmentation_metric(void) {
    umm_heap_context_t *_context = umm_get_current_heap();
    #ifdef UMM_INLINE_METRICS
    compute_fragmentation_metric(_context);
    #else
    umm_info_ctx(_context, NULL, false);
    #endif
    DBGLOG_DEBUG("freeBlocks %i freeBlocksSquared %i\n", _context->info.freeBlocks, _context->info.freeBlocksSquared);

    return _context->info.fragmentation_metric;
}

#ifdef UMM_INLINE_METRICS
static void umm_fragmentation_metric_init(umm_heap_context_t *_context) {
    _context->info.freeBlocks = UMM_NUMBLOCKS - 2;
    _context->info.freeBlocksSquared = _context->info.freeBlocks * _context->info.freeBlocks;
}

static void umm_fragmentation_metric_add(umm_heap_context_t *_context, uint16_t c) {
    uint16_t blocks = (UMM_NBLOCK(c) & UMM_BLOCKNO_MASK) - c;
    DBGLOG_DEBUG("Add block %d size %d to free metric\n", c, blocks);
    _context->info.freeBlocks += blocks;
    _context->info.freeBlocksSquared += (blocks * blocks);
}

static void umm_fragmentation_metric_remove(umm_heap_context_t *_context, uint16_t c) {
    uint16_t blocks = (UMM_NBLOCK(c) & UMM_BLOCKNO_MASK) - c;
    DBGLOG_DEBUG("Remove block %d size %d from free metric\n", c, blocks);
    _context->info.freeBlocks -= blocks;
    _context->info.freeBlocksSquared -= (blocks * blocks);
}
#endif // UMM_INLINE_METRICS

/* ------------------------------------------------------------------------ */
#endif

#endif  // defined(BUILD_UMM_MALLOC_C)

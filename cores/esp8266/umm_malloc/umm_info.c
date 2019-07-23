#if defined(UMM_INFO)

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

UMM_HEAP_INFO ummHeapInfo;

void *umm_info( void *ptr, int force ) {
  UMM_CRITICAL_DECL(id_info);

  unsigned short int blockNo = 0;

  /* Protect the critical section... */
  UMM_CRITICAL_ENTRY(id_info);

  /*
   * Clear out all of the entries in the ummHeapInfo structure before doing
   * any calculations..
   */
  memset( &ummHeapInfo, 0, sizeof( ummHeapInfo ) );

  DBGLOG_FORCE( force, "+----------+-------+--------+--------+-------+--------+--------+\n" );
  DBGLOG_FORCE( force, "|0x%08lx|B %5i|NB %5i|PB %5i|Z %5i|NF %5i|PF %5i|\n",
      (unsigned long)(&UMM_BLOCK(blockNo)),
      blockNo,
      UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
      UMM_PBLOCK(blockNo),
      (UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK )-blockNo,
      UMM_NFREE(blockNo),
      UMM_PFREE(blockNo) );

  /*
   * Now loop through the block lists, and keep track of the number and size
   * of used and free blocks. The terminating condition is an nb pointer with
   * a value of zero...
   */

  blockNo = UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK;

  while( UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK ) {
    size_t curBlocks = (UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK )-blockNo;

    ++ummHeapInfo.totalEntries;
    ummHeapInfo.totalBlocks += curBlocks;

    /* Is this a free block? */

    if( UMM_NBLOCK(blockNo) & UMM_FREELIST_MASK ) {
      ++ummHeapInfo.freeEntries;
      ummHeapInfo.freeBlocks += curBlocks;
      ummHeapInfo.freeSize2 += (unsigned int)curBlocks
                              * (unsigned int)sizeof(umm_block)
                              * (unsigned int)curBlocks
                              * (unsigned int)sizeof(umm_block);

      if (ummHeapInfo.maxFreeContiguousBlocks < curBlocks) {
        ummHeapInfo.maxFreeContiguousBlocks = curBlocks;
      }

      DBGLOG_FORCE( force, "|0x%08lx|B %5i|NB %5i|PB %5i|Z %5u|NF %5i|PF %5i|\n",
          (unsigned long)(&UMM_BLOCK(blockNo)),
          blockNo,
          UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
          UMM_PBLOCK(blockNo),
          (unsigned int)curBlocks,
          UMM_NFREE(blockNo),
          UMM_PFREE(blockNo) );

      /* Does this block address match the ptr we may be trying to free? */

      if( ptr == &UMM_BLOCK(blockNo) ) {

        /* Release the critical section... */
        UMM_CRITICAL_EXIT(id_info);

        return( ptr );
      }
    } else {
      ++ummHeapInfo.usedEntries;
      ummHeapInfo.usedBlocks += curBlocks;

      DBGLOG_FORCE( force, "|0x%08lx|B %5i|NB %5i|PB %5i|Z %5u|\n",
          (unsigned long)(&UMM_BLOCK(blockNo)),
          blockNo,
          UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
          UMM_PBLOCK(blockNo),
          (unsigned int)curBlocks );
    }

    blockNo = UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK;
  }

  /*
   * Update the accounting totals with information from the last block, the
   * rest must be free!
   */

  {
    size_t curBlocks = UMM_NUMBLOCKS-blockNo;
    ummHeapInfo.freeBlocks  += curBlocks;
    ummHeapInfo.totalBlocks += curBlocks;

    if (ummHeapInfo.maxFreeContiguousBlocks < curBlocks) {
      ummHeapInfo.maxFreeContiguousBlocks = curBlocks;
    }
  }

  DBGLOG_FORCE( force, "|0x%08lx|B %5i|NB %5i|PB %5i|Z %5i|NF %5i|PF %5i|\n",
      (unsigned long)(&UMM_BLOCK(blockNo)),
      blockNo,
      UMM_NBLOCK(blockNo) & UMM_BLOCKNO_MASK,
      UMM_PBLOCK(blockNo),
      UMM_NUMBLOCKS-blockNo,
      UMM_NFREE(blockNo),
      UMM_PFREE(blockNo) );

  DBGLOG_FORCE( force, "+----------+-------+--------+--------+-------+--------+--------+\n" );

  DBGLOG_FORCE( force, "Total Entries %5i    Used Entries %5i    Free Entries %5i\n",
      ummHeapInfo.totalEntries,
      ummHeapInfo.usedEntries,
      ummHeapInfo.freeEntries );

  DBGLOG_FORCE( force, "Total Blocks  %5i    Used Blocks  %5i    Free Blocks  %5i\n",
      ummHeapInfo.totalBlocks,
      ummHeapInfo.usedBlocks,
      ummHeapInfo.freeBlocks  );

  DBGLOG_FORCE( force, "+--------------------------------------------------------------+\n" );

#ifdef UMM_STATS
  if (ummHeapInfo.freeBlocks == ummStats.free_blocks) {
      DBGLOG_FORCE( force, "umm_info Free Blocks and umm stats Free Blocks match.\n");
  } else {
      DBGLOG_FORCE( force, "\nFree Blocks  %5d != umm stats Free Blocks  %5d\n\n",
          ummHeapInfo.freeBlocks,
          ummStats.free_blocks  );
  }

  DBGLOG_FORCE( force, "\numm stats:\n");
  DBGLOG_FORCE( force,   "  Free Space        %5u\n", ummStats.free_blocks * sizeof(umm_block));
  DBGLOG_FORCE( force,   "  Low Watermark     %5u\n", ummStats.free_blocks_min * sizeof(umm_block));
  DBGLOG_FORCE( force,   "  MAX Alloc Request %5u\n", ummStats.alloc_max_size);
  DBGLOG_FORCE( force,   "  OOM Count         %5u\n", ummStats.oom_count);
  DBGLOG_FORCE( force,   "  Size of umm_block %5u\n", sizeof(umm_block));
#endif

  DBGLOG_FORCE( force, "+--------------------------------------------------------------+\n" );

  /* Release the critical section... */
  UMM_CRITICAL_EXIT(id_info);

  return( NULL );
}

/* ------------------------------------------------------------------------ */

size_t umm_free_heap_size_info( void ) {
  umm_info(NULL, 0);
  return (size_t)ummHeapInfo.freeBlocks * sizeof(umm_block);
}

size_t umm_max_block_size( void ) {
  umm_info(NULL, 0);
  return ummHeapInfo.maxFreeContiguousBlocks * sizeof(umm_block);
}

size_t umm_block_size( void ) {
  return sizeof(umm_block);
}

/* ------------------------------------------------------------------------ */
#endif

#ifdef UMM_STATS

UMM_STATISTICS ummStats;

size_t umm_free_heap_size_lw( void ) {
  return (size_t)ummStats.free_blocks * sizeof(umm_block);
}

size_t umm_free_heap_size_lw_min( void ) {
  return (size_t)ummStats.free_blocks_min * sizeof(umm_block);
}

size_t umm_free_heap_size_min_reset( void ) {
  ummStats.free_blocks_min = ummStats.free_blocks;
  return (size_t)ummStats.free_blocks_min *  sizeof(umm_block);
}
#endif

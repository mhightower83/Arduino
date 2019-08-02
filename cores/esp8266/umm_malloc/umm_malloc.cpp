/*
 * Itemized changes started July 22, 2019
 * AUG 1, 2019 compiles with OOM, UMM_POISON_CHECK, and appears to be working.
 *
 * local changes for upstream umm_malloc:
 *
 * In malloc.c
 *   Added `#if defined(...)` to cover all of umm_malloc.c.
 *   commented out DBGLOG_LEVEL. Now defined in ...cfg.h
 *
 *   umm_free() - moved critical section to start after safe calculations.
 *
 *   umm_malloc() - moved critical section to start after umm_blocks()
 *   computations are based on contants that don't change, calling
 *   argument excepted.
 *
 *   umm_realloc() - Added UMM_CRITICAL_SUSPEND()/UMM_CRITICAL_RESUME() for
 *   when lightweight locks are available. eg. sti/cli. Single threaded
 *   single CPU case.
 *
 *   Added free heap tracking.
 *
 *
 * In umm_info.c
 *   umm_info() - Added UMM_CRITICAL_DECL(id_info), updated critical sections
 *   with tag.
 *
 *
 * In umm_poison.c:
 *   Resolved C++ compiler error reported on get_poisoned(), and
 *   get_unpoisoned(). They now take in void * arg instead of unsigned char *.
 *
 *
 * In umm_malloc_cfg.h:
 *   Added UMM_CRITICAL_SUSPEND()/UMM_CRITICAL_RESUME()
 *
 *
 * Globally change across all files %i to %d: umm_info.c, umm_malloc.c,
 *
 *
 * Notes,
 *
 *   umm_integrity_check() in umm_integrity.c needs a critical section.
 *   It also is still using printf instead of the DBGLOG_... macro.
 *   Looks to be primarily for testing during development of the malloc
 *   library. May also be useful in exterm heap corruption cases.
 *   Does not appear ready fo inclusion at this time,
 *
 *   umm_poison_check() in umm_poison.c needs a critical section.
 *
 */



#define _UMM_MALLOC_CPP
extern "C" {

#include "umm_malloc.c"


};

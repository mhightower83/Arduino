/*
 * Itemized changes started July 22, 2019
 *
 * local changes for upstream umm_malloc:
 *
 * In malloc.c
 *   Added `#if defined(...)` to cover all of umm_malloc.c.
 *   commented out DBGLOG_LEVEL. No defined in ...cfg.h
 *
 * umm_free() - moved critical section to start after safe calculations.
 *
 * umm_malloc() - moved critical section to start after umm_blocks() umm_blocks'
 * computations are based on contants that don't change, calling argument excepted.
 *
 * umm_realloc() - Added UMM_CRITICAL_SUSPEND()/UMM_CRITICAL_RESUME() for when
 * lightweight locks are available. eg. sti/cli. Single threaded single CPU case.
 *
 * umm_info() - Added UMM_CRITICAL_DECL(id_info), updated critical sections
 * with tag.
 *
 * Added UMM_CRITICAL_SUSPEND()/UMM_CRITICAL_RESUME() to umm_malloc_cfg.h
 *
 * Added free heap tracking.
 *
 * TODO: Globally change across all files %i to %d.
 *
 * Notes,
 *   umm_integrity_check() in umm_integrity.c needs a critical section.
 *   umm_poison_check() in umm_poison.c needs a critical section.
 *
 */



#define _UMM_MALLOC_CPP
extern "C" {

#include "umm_malloc.c"


};

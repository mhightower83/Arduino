#ifndef ARD_DEBUG_H
#define ARD_DEBUG_H

#include <stddef.h>
#include <stdint.h>
#include <c_types.h>

#ifdef DEBUG_ESP_CORE
#define DEBUGV(fmt, ...) ::printf((PGM_P)PSTR(fmt), ## __VA_ARGS__)
#endif

#ifndef DEBUGV
#define DEBUGV(...) do { (void)0; } while (0)
#endif

#ifdef __cplusplus
void hexdump(const void *mem, uint32_t len, uint8_t cols = 16);
#else
void hexdump(const void *mem, uint32_t len, uint8_t cols);
#endif

#ifdef __cplusplus
extern "C" {
#endif

void __panic_func(const char* file, int line, const char* func) __attribute__((noreturn));
#define panic() __panic_func(PSTR(__FILE__), __LINE__, __func__)

enum MSK_REPORT_LOG_CONTROL {
    MSK_REPORT_ASSERT_LOG_GDB_BREAK_ALL = BIT(30), // Use with set_report_log_control
    MSK_REPORT_ASSERT_LOG_GDB_BREAK = BIT(29),     // Use with assert_log ...
    MSK_REPORT_NO_STACK_TRACE = BIT(28),
    MSK_REPORT_NO_ABORT = BIT(27),
    MSK_REPORT_PS_REG_VALID = BIT(26),

    MSK_REPORT_FILTER_MASK = ((~0UL)>>6)
};

#ifdef DEBUG_ESP_PORT
// set filter mask for assert_log events
// gdb_debug is not normally called. Or in MSK_REPORT_ASSERT_LOG_GDB_BREAK_ALL
// to enable calling dbg_debug on all assert_log calls
int set_dbg_log_control(int mask);

void inline _restore_dbg_log_control(int *p) {
    set_dbg_log_control(*p);
}

// Set assert logging filter for the current scope.
// Previous logging control is restored after leaving scope
#define ASSERT_LOG_SCOPE_CONTROL(_m) int _old_dbg_mask##__LINE__ \
                __attribute__ ((__cleanup__(_restore_dbg_log_control))) = \
                set_dbg_log_control(_m)

// Diable assert logging for the current scope.
#define ASSERT_LOG_DISABLED_FOR_THIS_SCOPE() ASSERT_LOG_SCOPE_CONTROL(0)


void __assert_log_func(const char *file, int line,
                              const char *func, const char *what,
                              int mask, uint32_t ps_reg );

#define _assert_log_general(__e, __t, __m, __ps) \
        ((__e) || (__m == 0)) ? (void)0 : __assert_log_func( \
          PSTR(__FILE__), __LINE__, __ASSERT_FUNC, PSTR(#__e", "#__t), \
          (MSK_REPORT_NO_ABORT | __m), __ps )
// PSTR(#__e", "#__m", "#__t)
// Assert logged and continue. (no abort)
#define assert_log(__e, __t, __m) \
           _assert_log_general(__e, __t, (__m|MSK_REPORT_NO_STACK_TRACE), 0)
//
// Assert logged, stack trace and continue.
#define assert_log_trace(__e, __t, __m) _assert_log_general(__e, __t, __m, 0)


// Assert logged with supplied PS, stack trace and continue.
#define assert_log_trace_ps(__e, __t, __m, __ps) \
           _assert_log_general(__e, __t, (__m|MSK_REPORT_PS_REG_VALID), __ps)

#else
#define set_assert_log(__e) ((void)0)
#define assert_log(__e, __m) ((void)0)
#define assert_log_trace(__e, __m) ((void)0)
#define assert_log_trace_ps(__e, __m, __p) ((void)0)
#endif

#ifdef __cplusplus
}
#endif


#endif//ARD_DEBUG_H

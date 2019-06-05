#if 0 //ndef _INFLIGHT_DBG_H
#define _INFLIGHT_DBG_H

#ifdef __cplusplus
extern "C" {
#endif

int inflight_printf(const char *str, ...);
// void inflight_stack_trace(uint32_t ps_reg);


// #define dbg_printf inflight_printf

#ifdef __cplusplus
}
#endif

class PrintExecutionTime {
  private:
    const char *_txt;
    uint32_t _threshold;
    uint32_t _startCycle;

  public:
    static uint32_t _calibrationCycles;
    PrintExecutionTime(const char * txtIdentifier, const uint32_t threshold = 500000);
    ~PrintExecutionTime();
    uint32_t calibrate(uint32_t cal=0);
};

#endif  //#ifndef _INFLIGHT_DBG_H

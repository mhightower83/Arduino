#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Esp.h>
#include <user_interface.h>

#include <umm_malloc/umm_malloc.h>
void enable_debug_hwdt_at_link_time(void);

// #include <coredecls.h> // for disable_extra4k_at_link_time();

#include "hwdt_stack_dump.h"

constexpr volatile uint32_t * RTC_SYS = (volatile uint32_t*)0x60001100;
extern struct rst_info resetInfo;

void setup(void)
{
  enable_debug_hwdt_at_link_time();
  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);
  delay(20);
  Serial.println();
  Serial.println();
  Serial.println(String(F("RTC_SYS[0] = ")) +  (stack_usages.rtc_sys_reason) + F(", resetInfo.reason = ") + (resetInfo.reason) + F(", ") + ESP.getResetReason());
  if (stack_usages.sys) {
  Serial.println(String(F("Stack Usages:")));
    Serial.printf_P(PSTR("  ctx: sys  %6u\r\n"), stack_usages.sys);
    Serial.printf_P(PSTR("  ctx: cont %6u\r\n"), stack_usages.cont);
    if (stack_usages.rom) {
      Serial.printf_P(PSTR("  ctx: ROM  %6u\r\n"), stack_usages.rom);
    }
  }
  Serial.println();
  Serial.println(F("Up and running ..."));
  Serial.println();
  processKey(Serial, '?');
}

int* nullPointer = NULL;

void processKey(Print& out, int hotKey) {
  switch (hotKey) {
    case 'r':
      out.printf("Reset, ESP.reset(); (%ld ms) ...\n", millis());
      ESP.reset();
      break;
    case 't':
      out.printf("Restart, ESP.restart(); (%ld ms) ...\n", millis());
      ESP.restart();
      break;
    case 's':
      out.printf("Crash with Software WDT (%ld ms) ...\n", millis());
      while (true) {
        // Wait for Software WDT to kick in.
      }
      break;
    case 'h':
      out.printf("Crash with Hardware WDT (%ld ms) ...\n", millis());
      // ESP.wdtDisable();
      asm volatile("" ::: "memory");
      asm volatile ("mov.n a2, %0\n"
                    "mov.n a3, %1\n"
                    "mov.n a4, %1\n"
                    "mov.n a5, %1\n"
                    "mov.n a6, %1\n"
                    : : "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa) );
      // Could not find a stack save in the stack dumps, unless interrupts were enabled.
      while (true) {
        xt_rsil(15);
        // stay in an infinite loop doing nothing
        // this way other process can not be executed
        //
        // Note:
        // Hardware WDT kicks in if Software WDT is unable to perfrom.
        // Nothing will be saved on the stack for the Hardware WDT
      }
      break;

    case 'z':
      out.printf("Add a test (%ld ms) ...\n", millis());
      break;
    case 'm':
      {
        printDramMap(Serial);
        break;
      }
    case 'u':
      {
        umm_info(NULL, true);
        uint16_t hmax = 0;
        uint8_t  hfrag = 0;
        uint32_t hfree = 0;
        ESP.getHeapStats(&hfree, &hmax, &hfrag);
        out.printf("Heap Free Space:      %5u Bytes", hfree);
        out.println();
        out.printf("Heap Free Contiguous: %5u Bytes", hmax);
        out.println();
        out.printf("Heap Fragmentation:   %5u %%", hfrag);
        out.println();
      }
      break;
    case '\n':
    case '\r':
      break;
    default:
      out.printf("\"%c\" - Not an option?  / ? - help", hotKey);
      out.println();
    case '?':
      out.println();
      out.println("Press a key + <enter>");
      out.println("  r    - Reset, ESP.reset();");
      out.println("  t    - Restart, ESP.restart();");
      out.println("  u    - Print Heap Info, umm_info(NULL, true);");
      out.println("  m    - DRAM Memory Map");
      out.println("  z    - Test dejour");
      out.println("  ?    - Print Help");
      out.println();
      out.println("Crash with:");
      out.println("  s    - Software WDT");
      out.println("  h    - Hardware WDT");
      out.println();
      break;
  }
}

void loop(void) {
  if (Serial.available() > 0) {
    int hotKey = Serial.read();
    processKey(Serial, hotKey);
  }
}

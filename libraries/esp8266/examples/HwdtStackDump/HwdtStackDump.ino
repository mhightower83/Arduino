#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Esp.h>
#include <user_interface.h>

#include <umm_malloc/umm_malloc.h>
//void enable_debug_hwdt_at_link_time(void);
#define SWRST do { (*((volatile uint32_t*) 0x60000700)) |= 0x80000000; } while(0);

#include <coredecls.h> // for disable_extra4k_at_link_time();

#include "hwdt_stack_dump.h"

extern "C" uint32_t rtc_get_reset_reason(void);
constexpr volatile uint32_t * RTC_SYS = (volatile uint32_t*)0x60001100;
extern struct rst_info resetInfo;
//extern uint32_t *g_romStack;

void setup(void)
{
//  enable_debug_hwdt_at_link_time();
  WiFi.mode(WIFI_OFF);
  //  before_setup = false;
  Serial.begin(115200);
  delay(20);
  Serial.println();
  Serial.println();
//  Serial.printf("rtc_info_ptr = 0x%08X\n",  (uint32_t)system_get_rst_info());
//  Serial.println(String(F("rtc_get_reset_reason() = ")) + (g_romStack[0]) + F(", RTC_SYS[0] = ") +  ((g_romStack[1])) + F(", resetInfo.reason = ") + (resetInfo.reason) );
//  Serial.println(String(F("rtc_get_reset_reason() = ")) + (rtc_get_reset_reason()) + F(", RTC_SYS[0] = ") +  ((RTC_SYS[0])) + F(", resetInfo.reason = ") + (resetInfo.reason) );
  Serial.println(String(F("RTC_SYS[0] = ")) +  ((RTC_SYS[0])) + F(", resetInfo.reason = ") + (resetInfo.reason) );
  if (stack_usages.sys) {
  Serial.println(String(F("Stack Usages:")));
    Serial.printf_P(PSTR("  ctx: sys  %6u\r\n"), stack_usages.sys);
    Serial.printf_P(PSTR("  ctx: cont %6u\r\n"), stack_usages.cont);
    if (stack_usages.rom) {
      Serial.printf_P(PSTR("  ctx: ROM  %6u\r\n"), stack_usages.rom);
    }
  }
  Serial.println();
//  Serial.printf("0x%08X 0x%08X\n", g_romStack[2], g_romStack[3]);
  Serial.println(F("Up and running ..."));
  
  Serial.println();
}

int* nullPointer = NULL;

void loop(void)
{
  if (Serial.available() > 0)
  {
    char inChar = Serial.read();
    switch (inChar)
    {
      case 's':
        Serial.printf("Crashing with software WDT (%ld ms) ...\n", millis());
        while (true)
        {
          // stay in an infinite loop doing nothing
          // this way other process can not be executed
        }
        break;
      case 'r':
        Serial.printf("Reset ESP (%ld ms) ...\n", millis());
        ESP.reset();
        break;
      case 't':
        Serial.printf("Restart ESP (%ld ms) ...\n", millis());
        ESP.restart();
        break;
      case 'h':
        Serial.printf("Crashing with hardware WDT (%ld ms) ...\n", millis());
        ESP.wdtDisable();
        //        xt_rsil(15);
        asm volatile("" ::: "memory");
        asm volatile ("mov.n a2, %0\n"
                      "mov.n a3, %1\n"
                      "mov.n a4, %1\n"
                      "mov.n a5, %1\n"
                      "mov.n a6, %1\n"
                      : : "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa) );
        // Could not find a stack save in the stack dumps, unless interrupts were enabled.
        while (true)
        {
          // stay in an infinite loop doing nothing
          // this way other process can not be executed
          //
          // Note:
          // Hardware wdt kicks in if software wdt is unable to perfrom
          // Nothing will be saved in EEPROM for the hardware wdt
        }
        break;
      case '0':
        Serial.printf("Crashing with 'division by zero' exeption (%ld ms) ...\n", millis());
        int result, zero;
        zero = 0;
        result = 1 / zero;
        Serial.printf("Result = %d\n", result);
        break;
      case 'e':
        Serial.printf("Crashing with 'read through a pointer to no object' exeption (%ld ms) ...\n", millis());
        // null pointer dereference - read
        // attempt to read a value through a null pointer
        Serial.print(*nullPointer);
        break;
      case 'x':
        Serial.printf("Crashing with 'write through a pointer to no object' exeption (%ld ms) ...\n", millis());
        // null pointer dereference - write
        // attempt to write a value through a null pointer
        *nullPointer = 0;
        break;
      case 'z':
        Serial.printf("SWRST (%ld ms) ...\n", millis());
        // Software reset request through RTC register
        SWRST;
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
          Serial.printf("Heap Free Space:      %5u Bytes\n", hfree);
          Serial.printf("Heap Free Contiguous: %5u Bytes\n", hmax);
          Serial.printf("Heap Fragmentation:   %5u %%\n", hfrag);
        }
        break;
      //      case 'z':
      //        print_eventlog(Serial);
      //        break;

      case '?':
        Serial.println("\nPress a key + <enter>");
        Serial.println("<space> : print crash information");
        Serial.println("r/t : reset / restart module");
        Serial.println("? : print help");
        Serial.println("Crash this application with");
        Serial.println("s/h : software / harware WDT");
        Serial.println("0 : 'division by zero' exeption");
        Serial.println("e : 'read through a pointer to no object' exeption");
        Serial.println("x : 'write through a pointer to no object' exeption");
        Serial.println("i : 'write through a pointer iRAM' exeption");
        Serial.println("u : umm info");
        Serial.println("m : DRAM Memory Map");
        Serial.println("z : Test dejour");

        break;
      case '\n':
      case '\r':
        break;

      default:
        Serial.printf("Case? (%c) / ? - help\n", inChar);
        break;
    }
  }
}

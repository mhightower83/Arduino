#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Esp.h>
#include <user_interface.h>

#include <umm_malloc/umm_malloc.h>
void enable_debug_hwdt_at_link_time(void);

// #include <coredecls.h> // for disable_extra4k_at_link_time();

#include "hwdt_stack_dump.h"

extern struct rst_info resetInfo;

void setup(void)
{
  enable_debug_hwdt_at_link_time();
  WiFi.persistent(false); // w/o this a flash write occurs at every boot
  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);
  delay(20);
  Serial.println();
  Serial.println();
  Serial.println(String(F("RTC_SYS[0] = ")) +  (hwdt_info.rtc_sys_reason) + F(", resetInfo.reason = ") + (resetInfo.reason) + F(", ") + ESP.getResetReason());
  if (hwdt_info.sys) {
  Serial.println(String(F("Stack Usages:")));
    Serial.printf_P(PSTR("  ctx: sys  %6u\r\n"), hwdt_info.sys);
    uint32 cont_flags = hwdt_info.cont_integrity;
    Serial.printf_P(PSTR("  ctx: cont %6u, Integrity Flags: %04X - %s\r\n"), hwdt_info.cont, cont_flags, (cont_flags) ? "fail" : "pass");
    if (hwdt_info.rom) {
      Serial.printf_P(PSTR("  ctx: ROM  %6u\r\n"), hwdt_info.rom);
    }
  }
  Serial.println();
  Serial.println(F("Up and running ..."));
  Serial.println();
  processKey(Serial, '?');
}

int* nullPointer = NULL;

void loop(void) {
  if (Serial.available() > 0) {
    int hotKey = Serial.read();
    processKey(Serial, hotKey);
  }
}

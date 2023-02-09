/*
  This example is a variation on BasicOTA.

  Logic added to look for a change in SDK Version. If so, erase the WiFi
  Settings and Reset the system.

  Added extra debug printing to aid in cutting through the confusion of the
  multiple reboots.
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

// You can control the extra debug printing here. To turn off, change 1 to 0.
#if 1
#ifdef DEBUG_ESP_PORT
#define CONSOLE DEBUG_ESP_PORT
#else
#define CONSOLE Serial
#endif
#define DEBUG_PRINTF(fmt, ...) CONSOLE.printf_P(PSTR(fmt), ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(...)
#endif

#ifndef STASSID
#define STASSID "your-ssid"
#define STAPSK "your-password"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;

struct YourEEPROMData {
  // list of parameters you need to keep
  // ...

  // To efficiently save and compare SDK version strings, we use their computed
  // CRC32 value.
  uint32_t sdkCrc;
};

bool checkSdkCrc() {
  auto reason = ESP.getResetInfoPtr()->reason;
  // In this example, the OTA update does a software restart. As coded, SDK
  // version checks are only performed after a hard reset. Change the lines
  // below at your discretion.
  //
  // Boot loop guard
  // Limit crash loops erasing flash. Only run at Power On or Hardware Reset.
  if (REASON_DEFAULT_RST != reason && REASON_EXT_SYS_RST != reason) {
    DEBUG_PRINTF("  Boot loop guard - SDK version not checked. To perform check, do a hardware reset.\r\n");
    return true;
  }

  const char* sdkVerStr = ESP.getSdkVersion();
  uint32_t sdkVersionCrc = crc32(sdkVerStr, strlen(sdkVerStr));

  uint32_t savedSdkVersionCrc;
  EEPROM.begin((sizeof(struct YourEEPROMData) + 3) & ~3);
  EEPROM.get(offsetof(struct YourEEPROMData, sdkCrc), savedSdkVersionCrc);

  DEBUG_PRINTF("  Current SDK Verison: %s CRC(0x%08X)\r\n", sdkVerStr, sdkVersionCrc);
  DEBUG_PRINTF("  Previous saved SDK CRC(0x%08X)\r\n", savedSdkVersionCrc);
  if (sdkVersionCrc == savedSdkVersionCrc) {
    return EEPROM.end();
  }

  DEBUG_PRINTF("  Handle wew SDK Version\r\n");
  // Remember new SDK CRC
  EEPROM.put(offsetof(struct YourEEPROMData, sdkCrc), sdkVersionCrc);
  if (EEPROM.commit() && EEPROM.end()) {
    // Erase WiFi Settings and Reset
    DEBUG_PRINTF("  EEPROM update successful. New SDK CRC saved.\r\n");
    DEBUG_PRINTF("  Erase config and reset: ...\r\n");
    ArduinoOTA.eraseConfigAndReset();  // Only returns on fail
    DEBUG_PRINTF("  ArduinoOTA.eraseConfigAndReset() failed!\r\n");

  } else {
    DEBUG_PRINTF("  EEPROM.commit() or EEPROM.end() failed!\r\n");
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  // It is normal for resets generated by "ArduinoOTA.eraseConfigAndReset()"
  // to be reported as "External System".
  Serial.println(String("Reset Reason: ") + ESP.getResetReason());
  Serial.println("Check for changes in SDK Version:");
  if (checkSdkCrc()) {
    Serial.println("  SDK version has not changed.");
  } else {
    Serial.println("  SDK version changed and update to saved details failed.");
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  ArduinoOTA.handle();
}

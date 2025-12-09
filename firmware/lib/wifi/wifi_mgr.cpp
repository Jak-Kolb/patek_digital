#include "wifi_mgr.h"
// #include <Arduino.h>
#include "app_config.h"

// #if ENABLE_WIFI
#include <WiFi.h>
// #else

#if __has_include(<wifi_secrets.h>)
#include <wifi_secrets.h>
#define WIFI_SECRETS_PRESENT 1
#else
#define WIFI_SECRETS_PRESENT 0
#endif

namespace wifi_mgr {

void begin() {
  // Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Serial.printf("[WIFI] Connecting to %s\n", WIFI_SSID);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    // Serial.print(".");
    if (millis() - start > 10000) {  // 10 second timeout
      // Serial.println("\n[WIFI] Connection timed out.");
      // Serial.println("\nRetrying...");
      WiFi.disconnect(true);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      start = millis();
      return;
    }
  }

  // Serial.println("\nWiFi connected!");
  // Serial.print("IP address: ");
  // Serial.println(WiFi.localIP());

}

bool is_connected() {
  return WiFi.status() == WL_CONNECTED;
}

bool tick() {
  // Nothing needed here for now
  if (is_connected()) {
    // Optionally, print signal strength or other info
    // Serial.printf("Wifi Connected to %s\n", WiFi.SSID());
    return 1;
  }
  else {
    // Serial.println("Wifi Not Connected");
    begin();
    return 0;
  }
}

}  // namespace wifi_mgr
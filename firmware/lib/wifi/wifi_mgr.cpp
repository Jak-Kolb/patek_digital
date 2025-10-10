#include "wifi_mgr.h"

#include <Arduino.h>

#include "app_config.h"

#if ENABLE_WIFI
#include <WiFi.h>

#if __has_include(<wifi_secrets.h>)
#include <wifi_secrets.h>
#define WIFI_SECRETS_PRESENT 1
#else
#define WIFI_SECRETS_PRESENT 0
#endif

namespace {
constexpr uint32_t kConnectTimeoutMs = 15000;
bool gAttempted = false;
}

namespace wifi_mgr {

bool begin() {
  if (!has_credentials()) {
    Serial.println("[WIFI] Credentials missing; skipping connection.");
    return false;
  }
  if (gAttempted) {
    return WiFi.isConnected();
  }
  gAttempted = true;
  
  Serial.println("[WIFI] Initializing WiFi...");
  Serial.print("[WIFI] MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("[WIFI] Connecting to SSID: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kConnectTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] *** WiFi Connection Successful ***");
    Serial.print("[WIFI] Connected to SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("[WIFI] IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WIFI] MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("[WIFI] Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return true;
  }

  Serial.println("[WIFI] *** WiFi Connection Failed ***");
  Serial.print("[WIFI] Connection timed out after ");
  Serial.print(kConnectTimeoutMs / 1000);
  Serial.println(" seconds");
  Serial.print("[WIFI] WiFi Status: ");
  Serial.println(WiFi.status());
  return false;
}

void loop() {
  if (!gAttempted || !has_credentials()) {
    return;
  }
  if (WiFi.isConnected()) {
    return;
  }
  static uint32_t lastAttempt = 0;
  const uint32_t now = millis();
  if (now - lastAttempt < 10000) {
    return;
  }
  lastAttempt = now;
  Serial.println("[WIFI] Attempting reconnection...");
  WiFi.reconnect();
}

void disconnect() {
  if (!has_credentials()) {
    return;
  }
  WiFi.disconnect();
}

bool is_connected() {
  return has_credentials() && WiFi.isConnected();
}

bool has_credentials() {
#if WIFI_SECRETS_PRESENT
  return true;
#else
  return false;
#endif
}

String ip_string() {
  if (!is_connected()) {
    return String();
  }
  return WiFi.localIP().toString();
}

String mac_address() {
#if WIFI_SECRETS_PRESENT
  return WiFi.macAddress();
#else
  return String();
#endif
}

String connected_ssid() {
  if (!is_connected()) {
    return String();
  }
  return WiFi.SSID();
}

}  // namespace wifi_mgr

#else  // ENABLE_WIFI == 0

namespace wifi_mgr {

bool begin() {
  Serial.println("[WIFI] Disabled via ENABLE_WIFI=0.");
  return false;
}

void loop() {}

void disconnect() {}

bool is_connected() { return false; }

bool has_credentials() { return false; }

String ip_string() { return String(); }

String mac_address() { return String(); }

String connected_ssid() { return String(); }

}  // namespace wifi_mgr

#endif

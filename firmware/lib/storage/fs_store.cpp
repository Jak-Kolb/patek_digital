#include "fs_store.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "app_config.h"

namespace fs_store {

void begin() {
  
  // Attempt to mount LittleFS, formatting if necessary
  if (!LittleFS.begin(true, "/littlefs", 5, "littlefs")) {
    Serial.println("LittleFS mount failed! Attempting to format...");
    if (!LittleFS.begin(true, "/littlefs", 5, "littlefs")) {
      Serial.println("LittleFS format also failed!");
      return; // Don't continue if filesystem fails
    }
    Serial.println("LittleFS formatted and mounted successfully");
  } else {
    Serial.println("LittleFS mounted successfully");
  }
  
  Serial.printf("LittleFS total=%u used=%u free≈%u bytes\n",
                (unsigned)LittleFS.totalBytes(),
                (unsigned)LittleFS.usedBytes(),
                (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
  }


}  // namespace fs_store

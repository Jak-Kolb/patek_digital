#include "fs_store.h"
#include <Arduino.h>
#include <LittleFS.h>

#include "app_config.h"



namespace fs_store {

const char* partition_name = "littlefs";
static const char* data_file_path = "/stored_data.bin";

bool begin(bool formatOnFail) {
  
  // Attempt to mount LittleFS, formatting if necessary
  if (!LittleFS.begin(formatOnFail)) return false;

  Serial.printf("LittleFS total=%u used=%u free≈%u bytes\n",
                (unsigned)LittleFS.totalBytes(),
                (unsigned)LittleFS.usedBytes(),
                (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));

  File fp = LittleFS.open(data_file_path, "a"); // Ensure file exists
  if (!fp) return false;
  fp.close();
  return true;

}


bool append(const int32_t vals[4]){
  File fp = LittleFS.open(data_file_path, "a");
  if (!fp) return false;
  size_t written = fp.write((const uint8_t*)vals, sizeof(int32_t)*4);
  fp.close();
  return written == sizeof(int32_t)*4;
}

// print data in filesystem
void printData() {
  File fp = LittleFS.open(data_file_path, "r");
  

}


// return size of filesystem
size_t size() {
  if (!LittleFS.exists(DATA_FILE)) {
    return 0;
  }
  
  File file = LittleFS.open(DATA_FILE, "r");
  if (!file) {
    Serial.println("[FS_STORE] Failed to open data file for size check");
    return 0;
  }
  
  size_t fileSize = file.size();
  file.close();
  return fileSize;
}
 

}  // namespace fs_store


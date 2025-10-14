#include "fs_store.h"
#include <Arduino.h>
#include <LittleFS.h>

#include "app_config.h"



namespace fs_store {

const char* partition_name = "littlefs";
static const char* data_file_path = "/stored_data.bin";
// Partition base address (flash offset) as defined in partitions_3m_fs.csv
static const size_t PARTITION_BASE_ADDR = 0x200000;

bool begin(bool formatOnFail) {
  
  // Attempt to mount LittleFS, formatting if necessary.
  // Provide mount path and partition label to avoid defaulting to "spiffs" partition name.
  if (!LittleFS.begin(formatOnFail, "/littlefs", 5, "littlefs")) {
    // If first attempt failed and formatOnFail was false, try formatting once.
    if (!formatOnFail && !LittleFS.begin(true, "/littlefs", 5, "littlefs")) {
      return false;
    }
  }

  Serial.printf("LittleFS total=%u used=%u free≈%u bytes\n",
                (unsigned)LittleFS.totalBytes(),
                (unsigned)LittleFS.usedBytes(),
                (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));

  File fp = LittleFS.open(data_file_path, "a"); // Ensure file exists
  if (!fp) return false;
  fp.close();
  return true;

}

// append 4 int32_t values to file
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
  if (!fp) {
    Serial.println("fs_store: Failed to open data file for reading");
    return;
  }
  Serial.println("fs_store: Stored Data (offset | abs_addr):");
  while (fp.available()) {
    size_t offset = fp.position();
    int32_t vals[4];
    size_t read = fp.read((uint8_t*)vals, sizeof(int32_t)*4);
    if (read != sizeof(int32_t)*4) {
      Serial.println("fs_store: Incomplete data read");
      break;
    }
    size_t abs_addr = PARTITION_BASE_ADDR + offset;
    Serial.printf("fs_store: offset=%6u | addr=0x%06X: %d %d %d %d\n", (unsigned)offset, (unsigned)abs_addr, vals[0], vals[1], vals[2], vals[3]);
  }
  fp.close();
}


// return size of filesystem
size_t size() {
  if (!LittleFS.exists(data_file_path)) {
    return 0;
  }

  File file = LittleFS.open(data_file_path, "r");
  if (!file) {
    Serial.println("fs_store: Failed to open data file for size check");
    return 0;
  }
  
  size_t fileSize = file.size();
  file.close();
  return fileSize;
}
 
bool erase() {
  if (LittleFS.exists(data_file_path)) {
    return LittleFS.remove(data_file_path);
  }
  return true; // File doesn't exist, consider it "erased"

}  // namespace fs_store
}


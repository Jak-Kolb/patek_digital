#include "fs_store.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ctime>

#include "app_config.h"



namespace fs_store {

const char* partition_name = "littlefs";
static constexpr const char* kDataFilePath = kFsDataPath;
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

  // Serial.printf("LittleFS total=%u used=%u free≈%u bytes\n",
  //               (unsigned)LittleFS.totalBytes(),
  //               (unsigned)LittleFS.usedBytes(),
  //               (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));

  File fp = LittleFS.open(kDataFilePath, "a");  // Ensure file exists
  if (!fp) return false;
  fp.close();
  return true;

}

bool append(const consolidate::ConsolidatedRecord& record){
  File fp = LittleFS.open(kDataFilePath, "a");
  if (!fp) return false;
  size_t written = fp.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record));
  fp.close();
  return written == sizeof(record);
}

// print data in filesystem
void printData() {
  File fp = LittleFS.open(kDataFilePath, "r");
  if (!fp) {
    // Serial.println("fs_store: Failed to open data file for reading");
    return;
  }
  // Serial.println("fs_store: Stored Data (offset | abs_addr):");
  while (fp.available()) {
    size_t offset = fp.position();
    consolidate::ConsolidatedRecord record{};
    size_t read = fp.read(reinterpret_cast<uint8_t*>(&record), sizeof(record));
    if (read != sizeof(record)) {
      // Serial.println("fs_store: Incomplete data read");
      break;
    }
    size_t abs_addr = PARTITION_BASE_ADDR + offset;
    const time_t epoch_sec = static_cast<time_t>(record.timestamp);
    const struct tm* tm_ptr = gmtime(&epoch_sec);
    char time_buf[24] = {0};
    if (tm_ptr != nullptr && epoch_sec > 1000000000) {
      struct tm tm_copy = *tm_ptr;
      strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_copy);
    }

    // Serial.printf(
    //     "fs_store: offset=%6u | addr=0x%06X: HR=%.1f bpm Temp=%.2f C Steps=%u ts=%sZ\n",
    //     (unsigned)offset,
    //     (unsigned)abs_addr,
    //     record.avg_hr_x10 / 10.0f,
    //     record.avg_temp_x100 / 100.0f,
    //     record.step_count,
    //     time_buf[0] ? time_buf : "(unset)");
  }
  fp.close();
}


// return size of filesystem
size_t size() {
  if (!LittleFS.exists(kDataFilePath)) {
    return 0;
  }

  File file = LittleFS.open(kDataFilePath, "r");
  if (!file) {
    // Serial.println("fs_store: Failed to open data file for size check");
    return 0;
  }
  
  size_t fileSize = file.size();
  file.close();
  return fileSize;
}
 
bool erase() {
  if (LittleFS.exists(kDataFilePath)) {
    return LittleFS.remove(kDataFilePath);
  }
  return true; // File doesn't exist, consider it "erased"
} 

size_t record_count() {
  const size_t file_size = size();
  return file_size / sizeof(consolidate::ConsolidatedRecord);
}

void for_each_record(const std::function<bool(const consolidate::ConsolidatedRecord&, size_t)>& callback) {
  if (!callback) {
    return;
  }

  File fp = LittleFS.open(kDataFilePath, "r");
  if (!fp) {
    // Serial.println("[FS_STORE] Failed to open data file for iteration");
    return;
  }

  size_t index = 0;
  while (fp.available()) {
    consolidate::ConsolidatedRecord record{};
    size_t read_bytes = fp.read(reinterpret_cast<uint8_t*>(&record), sizeof(record));
    
    if (read_bytes != sizeof(record)) {
      // Serial.printf("[FS_STORE] Incomplete record at index %u\n", static_cast<unsigned>(index));
      break;
    }

    // Call the callback with the record and index
    if (!callback(record, index)) {
      break;  // Callback returned false, stop iteration
    }
    
    index++;
  }

  fp.close();
}

}  // namespace fs_store

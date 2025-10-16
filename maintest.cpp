#include "fs_store.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "app_config.h"

namespace fs_store {

static const char* DATA_FILE = "/consolidated_data.bin";

bool begin() {
  // Attempt to mount LittleFS, formatting if necessary
  if (!LittleFS.begin(true, "/littlefs", 5, "littlefs")) {
    Serial.println("[FS_STORE] LittleFS mount failed! Attempting to format...");
    if (!LittleFS.begin(true, "/littlefs", 5, "littlefs")) {
      Serial.println("[FS_STORE] LittleFS format also failed!");
      return false;
    }
    Serial.println("[FS_STORE] LittleFS formatted and mounted successfully");
  } else {
    Serial.println("[FS_STORE] LittleFS mounted successfully");
  }
  
  Serial.printf("[FS_STORE] LittleFS total=%u used=%u free≈%u bytes\n",
                (unsigned)LittleFS.totalBytes(),
                (unsigned)LittleFS.usedBytes(),
                (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
  
  return true;
}

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

bool append(const std::vector<uint8_t>& data) {
  File file = LittleFS.open(DATA_FILE, "a");
  if (!file) {
    Serial.println("[FS_STORE] Failed to open data file for append");
    return false;
  }
  
  size_t written = file.write(data.data(), data.size());
  file.close();
  
  if (written != data.size()) {
    Serial.printf("[FS_STORE] Write error: expected %d bytes, wrote %d bytes\n", 
                  data.size(), written);
    return false;
  }
  
  Serial.printf("[FS_STORE] Appended %d bytes to data file\n", data.size());
  return true;
}

bool append_consolidated(const int32_t consolidated[4]) {
  // Convert int32_t array to byte vector for storage
  std::vector<uint8_t> data;
  data.reserve(16); // 4 * sizeof(int32_t)
  
  for (int i = 0; i < 4; i++) {
    // Store as little-endian bytes
    data.push_back((consolidated[i] >> 0) & 0xFF);
    data.push_back((consolidated[i] >> 8) & 0xFF);
    data.push_back((consolidated[i] >> 16) & 0xFF);
    data.push_back((consolidated[i] >> 24) & 0xFF);
  }
  
  return append(data);
}

bool append_consolidated_with_timestamp(const int32_t consolidated[4]) {
  // Get current timestamp (milliseconds since boot)
  uint32_t timestamp = millis();
  
  // Create data vector: 4 bytes timestamp + 16 bytes consolidated data
  std::vector<uint8_t> data;
  data.reserve(20);
  
  // Add timestamp as little-endian
  data.push_back((timestamp >> 0) & 0xFF);
  data.push_back((timestamp >> 8) & 0xFF);
  data.push_back((timestamp >> 16) & 0xFF);
  data.push_back((timestamp >> 24) & 0xFF);
  
  // Add consolidated data
  for (int i = 0; i < 4; i++) {
    data.push_back((consolidated[i] >> 0) & 0xFF);
    data.push_back((consolidated[i] >> 8) & 0xFF);
    data.push_back((consolidated[i] >> 16) & 0xFF);
    data.push_back((consolidated[i] >> 24) & 0xFF);
  }
  
  return append(data);
}

size_t list() {
  size_t fileSize = size();
  Serial.printf("[FS_STORE] Data file size: %d bytes\n", fileSize);
  
  if (fileSize == 0) {
    Serial.println("[FS_STORE] No data stored");
    return 0;
  }
  
  // If storing consolidated data only (16 bytes per entry)
  size_t entries = fileSize / 16;
  Serial.printf("[FS_STORE] Estimated %d consolidated data entries\n", entries);
  
  // If storing with timestamps (20 bytes per entry)
  if (fileSize % 20 == 0) {
    entries = fileSize / 20;
    Serial.printf("[FS_STORE] Or %d timestamped entries\n", entries);
  }
  
  return fileSize;
}

bool read_chunks(size_t offset, size_t length,
                 const std::function<void(const uint8_t*, size_t)>& on_chunk) {
  if (!LittleFS.exists(DATA_FILE)) {
    Serial.println("[FS_STORE] Data file does not exist");
    return false;
  }
  
  File file = LittleFS.open(DATA_FILE, "r");
  if (!file) {
    Serial.println("[FS_STORE] Failed to open data file for reading");
    return false;
  }
  
  if (!file.seek(offset)) {
    Serial.printf("[FS_STORE] Failed to seek to offset %d\n", offset);
    file.close();
    return false;
  }
  
  const size_t CHUNK_SIZE = 256; // Read in 256-byte chunks
  uint8_t buffer[CHUNK_SIZE];
  size_t bytesRead = 0;
  
  while (bytesRead < length && file.available()) {
    size_t toRead = min(CHUNK_SIZE, length - bytesRead);
    size_t actualRead = file.read(buffer, toRead);
    
    if (actualRead == 0) {
      break; // End of file or error
    }
    
    on_chunk(buffer, actualRead);
    bytesRead += actualRead;
  }
  
  file.close();
  Serial.printf("[FS_STORE] Read %d bytes from data file\n", bytesRead);
  return bytesRead > 0;
}

bool read_latest_entries(size_t count, 
                        const std::function<void(uint32_t timestamp, const int32_t consolidated[4])>& on_entry) {
  size_t fileSize = size();
  if (fileSize == 0) {
    Serial.println("[FS_STORE] No data to read");
    return false;
  }
  
  // Assume timestamped entries (20 bytes each)
  const size_t ENTRY_SIZE = 20;
  size_t totalEntries = fileSize / ENTRY_SIZE;
  
  if (totalEntries == 0) {
    Serial.println("[FS_STORE] No complete entries found");
    return false;
  }
  
  size_t entriesToRead = min(count, totalEntries);
  size_t startOffset = fileSize - (entriesToRead * ENTRY_SIZE);
  
  File file = LittleFS.open(DATA_FILE, "r");
  if (!file) {
    Serial.println("[FS_STORE] Failed to open data file for reading");
    return false;
  }
  
  if (!file.seek(startOffset)) {
    Serial.printf("[FS_STORE] Failed to seek to offset %d\n", startOffset);
    file.close();
    return false;
  }
  
  uint8_t entryBuffer[ENTRY_SIZE];
  size_t entriesRead = 0;
  
  while (entriesRead < entriesToRead && file.available()) {
    if (file.read(entryBuffer, ENTRY_SIZE) != ENTRY_SIZE) {
      break; // Incomplete read
    }
    
    // Parse timestamp (first 4 bytes, little-endian)
    uint32_t timestamp = (uint32_t(entryBuffer[3]) << 24) |
                        (uint32_t(entryBuffer[2]) << 16) |
                        (uint32_t(entryBuffer[1]) << 8) |
                        uint32_t(entryBuffer[0]);
    
    // Parse consolidated data (next 16 bytes, 4 x int32_t, little-endian)
    int32_t consolidated[4];
    for (int i = 0; i < 4; i++) {
      int baseIdx = 4 + (i * 4);
      consolidated[i] = (int32_t(entryBuffer[baseIdx + 3]) << 24) |
                       (int32_t(entryBuffer[baseIdx + 2]) << 16) |
                       (int32_t(entryBuffer[baseIdx + 1]) << 8) |
                       int32_t(entryBuffer[baseIdx + 0]);
    }
    
    on_entry(timestamp, consolidated);
    entriesRead++;
  }
  
  file.close();
  Serial.printf("[FS_STORE] Read %d entries from data file\n", entriesRead);
  return entriesRead > 0;
}

bool erase() {
  if (LittleFS.exists(DATA_FILE)) {
    if (LittleFS.remove(DATA_FILE)) {
      Serial.println("[FS_STORE] Data file erased successfully");
      return true;
    } else {
      Serial.println("[FS_STORE] Failed to erase data file");
      return false;
    }
  } else {
    Serial.println("[FS_STORE] Data file does not exist");
    return true; // Consider this success
  }
}

}  // namespace fs_store

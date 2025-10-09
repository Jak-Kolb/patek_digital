#include "fs_store.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "app_config.h"

namespace fs_store {
namespace {
bool gMounted = false;

bool ensure_mounted() {
  if (gMounted) {
    return true;
  }
  if (LittleFS.begin(true)) {
    gMounted = true;
    return true;
  }
  Serial.println("[FS] Initial mount failed; attempting format...");
  if (!LittleFS.format()) {
    Serial.println("[FS] Format failed.");
    return false;
  }
  gMounted = LittleFS.begin();
  if (!gMounted) {
    Serial.println("[FS] Mount failed after format.");
  }
  return gMounted;
}

}  // namespace

bool begin() {
  return ensure_mounted();
}

size_t size() {
  if (!ensure_mounted()) {
    return 0;
  }
  File f = LittleFS.open(kFsDataPath, FILE_READ);
  if (!f) {
    return 0;
  }
  const size_t len = f.size();
  f.close();
  return len;
}

bool append(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    return true;
  }
  if (!ensure_mounted()) {
    return false;
  }
  File f = LittleFS.open(kFsDataPath, FILE_APPEND);
  if (!f) {
    Serial.println("[FS] Failed to open data file for append.");
    return false;
  }
  const size_t written = f.write(data.data(), data.size());
  f.close();
  if (written != data.size()) {
    Serial.println("[FS] Short write detected.");
    return false;
  }
  return true;
}

size_t list() {
  if (!ensure_mounted()) {
    return 0;
  }
  const size_t bytes = size();
  Serial.print("[FS] File: ");
  Serial.print(kFsDataPath);
  Serial.print(" bytes=");
  Serial.println(bytes);
  return bytes;
}

bool read_chunks(size_t offset, size_t length,
                 const std::function<void(const uint8_t*, size_t)>& on_chunk) {
  if (!ensure_mounted()) {
    return false;
  }
  size_t total_size = size();
  if (offset >= total_size) {
    return false;
  }
  if (length == 0 || offset + length > total_size) {
    length = total_size - offset;
  }
  File f = LittleFS.open(kFsDataPath, FILE_READ);
  if (!f) {
    Serial.println("[FS] Failed to open data file for reading.");
    return false;
  }
  if (!f.seek(offset)) {
    Serial.println("[FS] Seek failed.");
    f.close();
    return false;
  }

  std::vector<uint8_t> buffer(kFsChunkSize);
  size_t remaining = length;
  while (remaining > 0) {
    size_t to_read = remaining < buffer.size() ? remaining : buffer.size();
    size_t read = f.read(buffer.data(), to_read);
    if (read == 0) {
      break;
    }
    if (on_chunk) {
      on_chunk(buffer.data(), read);
    }
    remaining -= read;
  }
  f.close();
  return true;
}

bool erase() {
  if (!ensure_mounted()) {
    return false;
  }
  if (!LittleFS.exists(kFsDataPath)) {
    Serial.println("[FS] Nothing to erase.");
    return true;
  }
  if (!LittleFS.remove(kFsDataPath)) {
    Serial.println("[FS] Failed to remove data file.");
    return false;
  }
  Serial.println("[FS] Data file erased.");
  return true;
}

}  // namespace fs_store

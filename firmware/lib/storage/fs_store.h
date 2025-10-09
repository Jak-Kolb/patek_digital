#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace fs_store {

// Mount LittleFS, formatting if required.
bool begin();

// Return total bytes stored in the consolidated data file.
size_t size();

// Append binary data to the consolidated file.
bool append(const std::vector<uint8_t>& data);

// Enumerate stored dataset metadata. Returns current file size.
size_t list();

// Read a portion of the file and invoke the callback for each chunk.
bool read_chunks(size_t offset, size_t length,
                 const std::function<void(const uint8_t*, size_t)>& on_chunk);

// Remove the consolidated file.
bool erase();

}  // namespace fs_store

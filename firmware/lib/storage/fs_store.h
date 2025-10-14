#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace fs_store {

// Mount LittleFS, formatting if required.
bool begin(bool formatOnFail);

// Return total bytes stored in the consolidated data file.
size_t size();

bool append(const int32_t vals[4]); // Append binary data to the consolidated file.

void printData(); // print data stored in filesystem 

bool erase(); // Remove the consolidated file.


}  // namespace fs_store

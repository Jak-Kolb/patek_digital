#pragma once

#include <cstddef>
#include <cstdint>

#include "compute/consolidate.h"

namespace fs_store {

// Mount LittleFS, formatting if required.
bool begin(bool formatOnFail);

// Return total bytes stored in the consolidated data file.
size_t size();

bool append(const consolidate::ConsolidatedRecord& record);  // Append binary data.

void printData();  // print data stored in filesystem

bool erase(); // Remove the consolidated file.


}  // namespace fs_store

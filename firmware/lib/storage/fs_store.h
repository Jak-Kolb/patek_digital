#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "compute/consolidate.h"

namespace fs_store {

// Mount LittleFS, formatting if required.
bool begin(bool formatOnFail);

// Return total bytes stored in the consolidated data file.
size_t size();

bool append(const consolidate::ConsolidatedRecord& record);  // Append binary data.

size_t record_count();

bool for_each_record(const std::function<bool(const consolidate::ConsolidatedRecord&, size_t index)>& visitor);

void printData();  // print data stored in filesystem

bool erase(); // Remove the consolidated file.


}  // namespace fs_store

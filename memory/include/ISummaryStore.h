#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "memory/SummaryMemory.h"
#include "store/StoreResult.h"
#include "writeback/HierarchicalSummaryRequest.h"

namespace dasall::memory {

class IMemoryStore;

using ISummaryStore = IMemoryStore;

}  // namespace dasall::memory
#pragma once

#include <cstdint>
#include <string>

#include "MaintenanceReport.h"
#include "config/MemoryConfig.h"
#include "store/StoreResult.h"

namespace dasall::memory {

class IMemoryStore;

using IMaintenanceStore = IMemoryStore;

}  // namespace dasall::memory
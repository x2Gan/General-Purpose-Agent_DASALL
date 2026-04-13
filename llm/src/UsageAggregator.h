#pragma once

#include "NormalizedUsageRecord.h"

#include "adapters/AdapterCallResult.h"
#include "provider/ProviderCatalogRepository.h"

namespace dasall::llm {

class UsageAggregator {
 public:
  [[nodiscard]] NormalizedUsageRecord aggregate(
      const AdapterUsageFragment& usage_fragment,
      const provider::ProviderModelMetadata& model_metadata) const;
};

}  // namespace dasall::llm
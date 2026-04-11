#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "LLMSubsystemConfig.h"
#include "PromptAssetDescriptor.h"

namespace dasall::llm::prompt {

struct PromptCatalog {
  std::vector<PromptAssetDescriptor> descriptors;

  [[nodiscard]] bool has_consistent_values() const;
  [[nodiscard]] const PromptAssetDescriptor* find_release(std::string_view prompt_id,
                                                          std::string_view version) const;
};

class PromptAssetRepository {
 public:
  bool init(const PromptAssetSourceConfig& config);
  bool reload();

  [[nodiscard]] std::shared_ptr<const PromptCatalog> snapshot() const;
  [[nodiscard]] std::string last_error_message() const;

 private:
  PromptAssetSourceConfig config_;
  std::shared_ptr<const PromptCatalog> catalog_snapshot_;
  std::string last_error_message_;
  bool initialized_ = false;
};

}  // namespace dasall::llm::prompt
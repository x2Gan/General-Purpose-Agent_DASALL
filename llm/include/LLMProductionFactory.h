#pragma once

#include <memory>
#include <string>

#include "ILLMManager.h"

namespace dasall::infra::secret {

class ISecretBackend;

}  // namespace dasall::infra::secret

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::llm {

struct LLMProductionFactoryOptions {
  std::shared_ptr<infra::secret::ISecretBackend> secret_backend;
};

struct LLMProductionFactoryResult {
  std::shared_ptr<ILLMManager> manager;
  std::string error;

  [[nodiscard]] bool ok() const {
    return manager != nullptr && error.empty();
  }
};

[[nodiscard]] LLMProductionFactoryResult create_production_llm_manager(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    LLMProductionFactoryOptions options = {});

}  // namespace dasall::llm
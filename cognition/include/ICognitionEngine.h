#pragma once

#include <memory>

#include "CognitionConfig.h"
#include "CognitionDependencies.h"
#include "CognitionTypes.h"

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::cognition {

class ICognitionEngine {
 public:
  virtual ~ICognitionEngine() = default;

  [[nodiscard]] virtual CognitionDecisionResult decide(
      const CognitionStepRequest& request) = 0;
  [[nodiscard]] virtual CognitionReflectionResult reflect(
      const ReflectionRequest& request) = 0;
};

[[nodiscard]] std::unique_ptr<ICognitionEngine> create_cognition_engine(
    const CognitionConfig& config = {});
[[nodiscard]] std::unique_ptr<ICognitionEngine> create_cognition_engine(
    const CognitionConfig& config,
    CognitionRuntimeDependencies dependencies);
[[nodiscard]] std::unique_ptr<ICognitionEngine> create_cognition_engine(
    const profiles::RuntimePolicySnapshot& snapshot,
    CognitionRuntimeDependencies dependencies = {});

}  // namespace dasall::cognition

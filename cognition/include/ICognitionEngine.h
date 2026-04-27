#pragma once

#include <memory>

#include "CognitionConfig.h"
#include "CognitionDependencies.h"
#include "CognitionTypes.h"

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

}  // namespace dasall::cognition

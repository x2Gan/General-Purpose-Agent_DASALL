#pragma once

#include <memory>

#include "CognitionConfig.h"
#include "CognitionDependencies.h"
#include "CognitionTypes.h"

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::cognition {

class IResponseBuilder {
 public:
  virtual ~IResponseBuilder() = default;

  [[nodiscard]] virtual ResponseBuildResult build(
      const ResponseBuildRequest& request) = 0;
};

[[nodiscard]] std::unique_ptr<IResponseBuilder> create_response_builder(
    const CognitionConfig& config = {});
[[nodiscard]] std::unique_ptr<IResponseBuilder> create_response_builder(
    const CognitionConfig& config,
    CognitionRuntimeDependencies dependencies);
[[nodiscard]] std::unique_ptr<IResponseBuilder> create_response_builder(
    const profiles::RuntimePolicySnapshot& snapshot,
    CognitionRuntimeDependencies dependencies = {});

}  // namespace dasall::cognition

#pragma once

#include <string>
#include <vector>

#include "ServiceTypes.h"
#include "adapters/AdapterBridge.h"
#include "adapters/AdapterRouter.h"
#include "mapping/ResultMapper.h"

namespace dasall::services::internal {

class ServiceTraceBridge;

struct ExecutionDiagnoseServiceDependencies {
  const AdapterRouter* router = nullptr;
  const AdapterBridge* bridge = nullptr;
  const ResultMapper* result_mapper = nullptr;

  ServicePolicyView policy_view;
  CapabilitySnapshotView capability_snapshot;
  FallbackEnvelope fallback_envelope;
  std::vector<AdapterCandidateView> registered_candidates;
  ServiceTraceBridge* trace_bridge = nullptr;
};

class ExecutionDiagnoseService {
 public:
  explicit ExecutionDiagnoseService(ExecutionDiagnoseServiceDependencies dependencies);

  [[nodiscard]] ExecutionDiagnoseResult diagnose(
      const ServiceCallContext& context,
      const ExecutionDiagnoseRequest& request) const;

 private:
  [[nodiscard]] ExecutionDiagnoseResult make_runtime_failure(const std::string& message,
                                                             const std::string& stage,
                                                             const std::string& ref_id) const;
  [[nodiscard]] ExecutionDiagnoseResult make_error_result(
      const std::string& target_id,
      const std::string& receipt_ref,
      const std::string& provider_status_code,
      const std::string& message,
      const std::string& stage,
      std::vector<std::string> evidence_refs) const;

  ExecutionDiagnoseServiceDependencies dependencies_;
};

}  // namespace dasall::services::internal
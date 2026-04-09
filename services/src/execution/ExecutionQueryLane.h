#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ServiceTypes.h"
#include "adapters/AdapterBridge.h"
#include "adapters/AdapterRouter.h"
#include "mapping/ResultMapper.h"

namespace dasall::services::internal {

class ServiceMetricsBridge;

struct CachedExecutionQuerySnapshot {
  std::string state;
  std::string snapshot_json;
  std::string snapshot_ref;
};

struct ExecutionQueryLaneDependencies {
  const AdapterRouter* router = nullptr;
  const AdapterBridge* bridge = nullptr;
  const ResultMapper* result_mapper = nullptr;

  ServicePolicyView policy_view;
  CapabilitySnapshotView capability_snapshot;
  FallbackEnvelope fallback_envelope;
  std::vector<AdapterCandidateView> registered_candidates;

  std::function<std::optional<CachedExecutionQuerySnapshot>(
      const ExecutionQueryRequest& request)>
      load_cached_snapshot;
  std::function<std::string(const AdapterReceipt& receipt,
                            const ExecutionQueryRequest& request)>
      extract_state;
  ServiceMetricsBridge* metrics_bridge = nullptr;
};

class ExecutionQueryLane {
 public:
  explicit ExecutionQueryLane(ExecutionQueryLaneDependencies dependencies);

  [[nodiscard]] ExecutionQueryResult query_state(const ServiceCallContext& context,
                                                 const ExecutionQueryRequest& request) const;

 private:
  [[nodiscard]] ExecutionQueryResult make_runtime_failure(const std::string& message,
                                                          const std::string& stage,
                                                          const std::string& ref_id) const;
  [[nodiscard]] ExecutionQueryResult make_error_result(const std::string& target_id,
                                                       const std::string& receipt_ref,
                                                       const std::string& provider_status_code,
                                                       const std::string& message,
                                                       const std::string& stage,
                                                       std::vector<std::string> evidence_refs,
                                                       bool from_cache) const;

  ExecutionQueryLaneDependencies dependencies_;
};

}  // namespace dasall::services::internal
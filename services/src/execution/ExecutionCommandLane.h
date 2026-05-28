#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ServiceTypes.h"
#include "adapters/AdapterBridge.h"
#include "adapters/AdapterRouter.h"
#include "mapping/ResultMapper.h"

namespace dasall::services::internal {

class CompensationCatalog;
class ServiceAuditBridge;
class ServiceLoggingBridge;
class ServiceMetricsBridge;
class ServiceTraceBridge;

struct ExecutionCommandLaneDependencies {
  const AdapterRouter* router = nullptr;
  const AdapterBridge* bridge = nullptr;
  const ResultMapper* result_mapper = nullptr;
  const CompensationCatalog* compensation_catalog = nullptr;

  ServicePolicyView policy_view;
  CapabilitySnapshotView capability_snapshot;
  FallbackEnvelope fallback_envelope;
  std::vector<AdapterCandidateView> registered_candidates;
  std::function<CapabilityRouteView(const std::string& capability_id,
                                    AdapterRouteRequestKind request_kind)>
      resolve_route_view = {};

  std::vector<std::string> critical_actions;
  std::vector<std::string> high_risk_actions;
  bool allow_high_risk_actions = false;

  std::function<std::vector<std::string>(const std::string& capability_id,
                                         const std::string& action,
                                         const std::string& capability_version,
                                         const AdapterReceipt& receipt)>
      lookup_compensation_hints;
  std::function<std::string(const ExecutionCommandRequest& request)> make_execution_id;
  std::function<std::string(const ExecutionCompensationRequest& request)>
      make_compensation_execution_id;
  std::function<void(const std::string& serialization_key)> on_serialization_acquired;
  ServiceAuditBridge* audit_bridge = nullptr;
  ServiceMetricsBridge* metrics_bridge = nullptr;
  ServiceTraceBridge* trace_bridge = nullptr;
  ServiceLoggingBridge* logging_bridge = nullptr;
};

class ExecutionCommandLane {
 public:
  explicit ExecutionCommandLane(ExecutionCommandLaneDependencies dependencies);

  [[nodiscard]] ExecutionCommandResult execute(const ServiceCallContext& context,
                                               const ExecutionCommandRequest& request);
  [[nodiscard]] ExecutionCommandResult compensate(
      const ServiceCallContext& context,
      const ExecutionCompensationRequest& request);

 private:
  [[nodiscard]] ExecutionCommandResult execute_impl(const ServiceCallContext& context,
                                                    const CapabilityTargetRef& target,
                                                    const std::string& action,
                                                    const std::string& payload_json,
                                                    const std::string& execution_id,
                                                    const std::string& idempotency_cache_key,
                                                    bool critical_action,
                                                    bool high_risk_action,
                                                    bool audit_required);
  [[nodiscard]] ExecutionCommandResult make_runtime_failure(const std::string& message,
                                                            const std::string& stage,
                                                            const std::string& ref_id) const;
  [[nodiscard]] ExecutionCommandResult make_error_result(const std::string& target_id,
                                                         const std::string& receipt_ref,
                                                         const std::string& provider_status_code,
                                                         const std::string& message,
                                                         const std::string& stage,
                                                         std::vector<std::string> evidence_refs,
                                                         std::string execution_id) const;

  void cache_result(const std::string& cache_key, const ExecutionCommandResult& result);
  [[nodiscard]] bool try_get_cached_result(const std::string& cache_key,
                                           ExecutionCommandResult* result) const;
  [[nodiscard]] bool try_acquire_serialization_key(const std::string& serialization_key);
  void release_serialization_key(const std::string& serialization_key);

  ExecutionCommandLaneDependencies dependencies_;
  mutable std::mutex state_mutex_;
  std::unordered_map<std::string, ExecutionCommandResult> idempotency_cache_;
  std::unordered_set<std::string> in_flight_serialization_keys_;
};

}  // namespace dasall::services::internal
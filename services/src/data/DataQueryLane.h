#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ServiceTypes.h"
#include "adapters/AdapterBridge.h"
#include "adapters/AdapterRouter.h"
#include "data/DataProjectionCache.h"
#include "mapping/ResultMapper.h"

namespace dasall::services::internal {

class ServiceLoggingBridge;
class ServiceMetricsBridge;
class ServiceTraceBridge;

struct DataQueryLaneDependencies {
  const AdapterRouter* router = nullptr;
  const AdapterBridge* bridge = nullptr;
  const ResultMapper* result_mapper = nullptr;
  DataProjectionCache* projection_cache = nullptr;

  ServicePolicyView policy_view;
  CapabilitySnapshotView capability_snapshot;
  FallbackEnvelope fallback_envelope;
  std::vector<AdapterCandidateView> registered_candidates;
  std::function<CapabilityRouteView(const std::string& capability_id,
                                    AdapterRouteRequestKind request_kind)>
      resolve_route_view = {};
  ServiceMetricsBridge* metrics_bridge = nullptr;
  ServiceTraceBridge* trace_bridge = nullptr;
  ServiceLoggingBridge* logging_bridge = nullptr;
};

class DataQueryLane {
 public:
  explicit DataQueryLane(DataQueryLaneDependencies dependencies);

  [[nodiscard]] DataQueryResult query(const ServiceCallContext& context,
                                      const DataQueryRequest& request);
  [[nodiscard]] DataCatalogResult list_capabilities(const ServiceCallContext& context,
                                                    const DataCatalogRequest& request) const;

 private:
  [[nodiscard]] DataQueryResult make_runtime_query_failure(const std::string& message,
                                                           const std::string& stage,
                                                           const std::string& ref_id) const;
  [[nodiscard]] DataCatalogResult make_runtime_catalog_failure(const std::string& message,
                                                               const std::string& stage,
                                                               const std::string& ref_id) const;
  [[nodiscard]] DataQueryResult make_query_error_result(const std::string& dataset,
                                                        const std::string& receipt_ref,
                                                        const std::string& provider_status_code,
                                                        const std::string& message,
                                                        const std::string& stage,
                                                        std::vector<std::string> evidence_refs,
                                                        bool from_cache) const;
  [[nodiscard]] DataCatalogResult make_catalog_error_result(
      const std::string& target_class,
      const std::string& receipt_ref,
      const std::string& provider_status_code,
      const std::string& message,
      const std::string& stage,
      std::vector<std::string> evidence_refs) const;

  DataQueryLaneDependencies dependencies_;
};

}  // namespace dasall::services::internal
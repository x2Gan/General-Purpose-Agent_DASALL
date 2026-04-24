#pragma once

#include <atomic>
#include <optional>
#include <string>

#include "AccessErrors.h"
#include "AccessTypes.h"
#include "agent/AgentRequest.h"

namespace dasall::access {

// RequestNormalizationOutput 承载归一化阶段产物，避免在 access 主链中散落中间状态。
struct RequestNormalizationOutput {
  bool normalized = false;
  RuntimeDispatchRequest runtime_request;
  PublishEnvelope publish_context;
  dasall::contracts::AgentRequest agent_request;
  std::optional<AccessError> error;
};

// RequestNormalizer 负责将入口事实收敛为 AgentRequest + dispatch/publish sidecar。
class RequestNormalizer final {
 public:
  RequestNormalizer(AccessBootstrapConfig bootstrap_config = {},
                    AccessPublishView publish_view = {});

  [[nodiscard]] RequestNormalizationOutput normalize(
      const RuntimeDispatchRequest& request) const;

 private:
  struct TraceIdentityBundle {
    std::string request_id;
    std::string session_id;
    std::string trace_id;
  };

  [[nodiscard]] TraceIdentityBundle ensure_trace_ids(
      const RuntimeDispatchRequest& request) const;

  [[nodiscard]] dasall::contracts::AgentRequest project_agent_request(
      const RuntimeDispatchRequest& request,
      const TraceIdentityBundle& ids) const;

  [[nodiscard]] PublishEnvelope build_publish_context(
      const RuntimeDispatchRequest& request,
      const TraceIdentityBundle& ids) const;

  [[nodiscard]] static std::optional<std::string> context_value(
      const RuntimeDispatchRequest& request,
      const std::string& key);

  [[nodiscard]] static dasall::contracts::RequestChannel map_request_channel(
      const std::string& entry_type);

  [[nodiscard]] static std::int64_t now_epoch_millis();

  [[nodiscard]] std::string generate_stable_id(
      const std::string& prefix,
      const RuntimeDispatchRequest& request,
      std::size_t ordinal) const;

  AccessBootstrapConfig bootstrap_config_;
  AccessPublishView publish_view_;
  mutable std::atomic<std::size_t> id_counter_{0};
};

}  // namespace dasall::access

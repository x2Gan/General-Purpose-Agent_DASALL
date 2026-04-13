#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ILLMAdapter.h"
#include "ILLMTransport.h"
#include "stream/StreamSessionRef.h"

#include "AdapterCallResult.h"

namespace dasall::llm {

class LocalLLMAdapter final : public ILLMAdapter {
 public:
  explicit LocalLLMAdapter(std::shared_ptr<ILLMTransport> transport);

  bool init(const LLMAdapterConfig& config) override;
  AdapterCallResult generate(const contracts::LLMRequest& request) override;
  StreamSessionRef stream_generate(const contracts::LLMRequest& request,
                                   IStreamObserver* observer) override;
  HealthStatus health_check() override;

 private:
  [[nodiscard]] bool is_ready() const;
  [[nodiscard]] std::string resolve_model_id(std::string_view route_key) const;
  [[nodiscard]] LLMTransportRequest make_generate_request(
      const contracts::LLMRequest& request) const;
  [[nodiscard]] LLMTransportRequest make_health_request() const;
  [[nodiscard]] AdapterCallResult map_generate_response(
      const contracts::LLMRequest& request,
      const LLMTransportResponse& response) const;
  [[nodiscard]] AdapterCallResult make_failure_result(
      contracts::ResultCode result_code,
      std::string message,
      bool retryable,
      std::string stage) const;

  std::shared_ptr<ILLMTransport> transport_;
  std::optional<LLMAdapterConfig> config_;
};

}  // namespace dasall::llm
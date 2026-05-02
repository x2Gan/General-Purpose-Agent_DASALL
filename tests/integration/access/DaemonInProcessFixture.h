#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include "AsyncTaskRegistry.h"
#include "DaemonIntegrationHarness.h"

namespace dasall::tests::integration::access_support {

class DaemonInProcessFixture {
 public:
  explicit DaemonInProcessFixture(
      dasall::access::DaemonAccessPipelineOptions options,
      dasall::apps::daemon::DaemonBootstrapConfig config = {},
      const std::int32_t connect_deadline_ms = 50)
      : receipt_registry_(ensure_registry(options, config)),
        harness_(std::move(options), config, connect_deadline_ms) {}

  [[nodiscard]] dasall::apps::cli::CliIpcClient make_client() const {
    return harness_.make_client();
  }

  [[nodiscard]] dasall::apps::cli::DaemonClientResponse send_frame(
      const dasall::access::daemon::UdsRequestFrame& frame) const {
    return harness_.send_frame(frame);
  }

  [[nodiscard]] dasall::apps::cli::DaemonClientResponse send_frame(
      const dasall::access::daemon::UdsRequestFrame& frame,
      const std::int32_t deadline_ms) const {
    return harness_.send_frame(frame, deadline_ms);
  }

  [[nodiscard]] std::size_t active_connection_count() const {
    return harness_.active_connection_count();
  }

  [[nodiscard]] std::size_t receipt_active_count() const {
    return receipt_registry_ != nullptr ? receipt_registry_->size() : 0U;
  }

  [[nodiscard]] std::size_t prune_expired_receipts() const {
    return receipt_registry_ != nullptr ? receipt_registry_->prune_expired() : 0U;
  }

  [[nodiscard]] std::shared_ptr<dasall::access::AsyncTaskRegistry> receipt_registry() const {
    return receipt_registry_;
  }

  void stop(
      const std::chrono::milliseconds drain_timeout = std::chrono::milliseconds::zero()) {
    harness_.stop(drain_timeout);
  }

  [[nodiscard]] bool daemon_stopped_cleanly() const {
    return harness_.daemon_stopped_cleanly();
  }

 private:
  static std::shared_ptr<dasall::access::AsyncTaskRegistry> ensure_registry(
      dasall::access::DaemonAccessPipelineOptions& options,
      const dasall::apps::daemon::DaemonBootstrapConfig& config) {
    if (options.async_task_registry != nullptr) {
      return options.async_task_registry;
    }

    const auto ttl = config.receipt_ttl_sec > 0
                         ? std::chrono::seconds(config.receipt_ttl_sec)
                         : std::chrono::minutes(10);
    options.async_task_registry = std::make_shared<dasall::access::AsyncTaskRegistry>(
        "daemon-in-process-fixture",
        std::chrono::duration_cast<std::chrono::milliseconds>(ttl));
    return options.async_task_registry;
  }

  std::shared_ptr<dasall::access::AsyncTaskRegistry> receipt_registry_;
  DaemonIntegrationHarness harness_;
};

}  // namespace dasall::tests::integration::access_support
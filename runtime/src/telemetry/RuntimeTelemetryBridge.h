#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "RuntimeEventBus.h"
#include "fsm/StateTransitionTypes.h"

namespace dasall::contracts {
struct RecoveryOutcome;
}

namespace dasall::runtime {

struct BudgetDecision;
struct SafeModeDecision;

enum class RuntimeTelemetryKind : std::uint8_t {
  Transition = 0,
  BudgetReject,
  RecoveryReject,
  SafeMode,
};

struct RuntimeTelemetryContext {
  std::optional<std::string> request_id;
  std::optional<std::string> session_id;
  std::optional<std::string> trace_id;
  std::optional<std::string> turn_id;
  std::optional<std::string> checkpoint_id;
};

struct RuntimeTelemetryRecord {
  RuntimeTelemetryKind kind = RuntimeTelemetryKind::Transition;
  RuntimeEventEnvelope envelope{};
  std::string subject;
};

struct RuntimeTelemetryBridgeOptions {
  std::string runtime_instance_id;
  std::function<std::int64_t()> now_ms;
};

class RuntimeTelemetryBridge final {
 public:
  explicit RuntimeTelemetryBridge(
      std::shared_ptr<RuntimeEventBus> event_bus = nullptr,
      RuntimeTelemetryBridgeOptions options = {});

  [[nodiscard]] RuntimeTelemetryRecord emit_transition(
      RuntimeState from_state,
      RuntimeState to_state,
      const RuntimeTelemetryContext& context,
      std::string detail = {},
      std::optional<RuntimeErrorCode> error_code = std::nullopt);
  [[nodiscard]] RuntimeTelemetryRecord emit_budget_reject(
      const BudgetDecision& decision,
      const RuntimeTelemetryContext& context,
      std::string detail = {});
  [[nodiscard]] RuntimeTelemetryRecord emit_recovery_reject(
      const contracts::RecoveryOutcome& outcome,
      const RuntimeTelemetryContext& context,
      std::string detail = {});
  [[nodiscard]] RuntimeTelemetryRecord emit_safe_mode(
      const SafeModeDecision& decision,
      const RuntimeTelemetryContext& context,
      std::string detail = {});

  [[nodiscard]] std::vector<RuntimeTelemetryRecord> snapshot() const;
  [[nodiscard]] std::size_t emit_count() const;

 private:
  [[nodiscard]] static std::int64_t default_now_ms();
  [[nodiscard]] RuntimeTelemetryRecord store_and_publish(RuntimeTelemetryRecord record);
  void append_common_attributes(RuntimeEventEnvelope* envelope) const;

  std::shared_ptr<RuntimeEventBus> event_bus_;
  RuntimeTelemetryBridgeOptions options_{};
  mutable std::mutex records_mutex_;
  std::vector<RuntimeTelemetryRecord> records_;
};

}  // namespace dasall::runtime
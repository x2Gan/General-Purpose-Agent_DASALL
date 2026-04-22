#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "RuntimeErrorCode.h"

namespace dasall::runtime {

enum class RuntimeEventCategory : std::uint8_t {
  Transition = 0,
  BudgetReject,
  RecoveryReject,
  SafeMode,
  Health,
  Maintenance,
  Audit,
};

enum class RuntimeEventSeverity : std::uint8_t {
  Debug = 0,
  Info,
  Warning,
  Error,
};

struct RuntimeEventContext {
  std::optional<std::string> request_id;
  std::optional<std::string> session_id;
  std::optional<std::string> trace_id;
  std::optional<std::string> turn_id;
  std::optional<std::string> checkpoint_id;
};

struct RuntimeEventAttribute {
  std::string key;
  std::string value;

  [[nodiscard]] bool has_required_fields() const {
    return !key.empty();
  }
};

struct RuntimeEventEnvelope {
  std::uint64_t sequence = 0U;
  RuntimeEventCategory category = RuntimeEventCategory::Audit;
  RuntimeEventSeverity severity = RuntimeEventSeverity::Info;
  std::string event_name;
  std::string detail;
  RuntimeEventContext context{};
  std::optional<RuntimeErrorCode> error_code;
  std::vector<RuntimeEventAttribute> attributes;
  bool audit = false;
  std::int64_t timestamp_ms = 0;

  [[nodiscard]] bool has_minimum_fields() const {
    return !event_name.empty();
  }
};

using RuntimeEventHandler = std::function<void(const RuntimeEventEnvelope&)>;

struct RuntimeEventSubscription {
  std::uint64_t subscription_id = 0U;
  std::string event_name_filter;
  bool audit_only = false;

  [[nodiscard]] bool is_valid() const {
    return subscription_id != 0U;
  }
};

struct RuntimeEventPublishResult {
  bool accepted = false;
  bool dropped_oldest = false;
  std::uint64_t dropped_count = 0U;
  std::size_t queue_depth = 0U;
};

struct RuntimeEventBusOptions {
  std::size_t max_non_audit_queue_depth = 256U;
  std::function<std::int64_t()> now_ms;
};

class RuntimeEventBus final {
 public:
  explicit RuntimeEventBus(RuntimeEventBusOptions options = {});

  [[nodiscard]] RuntimeEventSubscription subscribe(RuntimeEventHandler handler);
  [[nodiscard]] RuntimeEventSubscription subscribe(
      std::string event_name_filter,
      RuntimeEventHandler handler,
      bool audit_only = false);
  bool unsubscribe(std::uint64_t subscription_id);

  [[nodiscard]] RuntimeEventPublishResult publish(RuntimeEventEnvelope event);
  [[nodiscard]] std::size_t dispatch_pending(
      std::size_t max_events = std::numeric_limits<std::size_t>::max());

  [[nodiscard]] std::vector<RuntimeEventEnvelope> pending_snapshot() const;
  [[nodiscard]] std::uint64_t drop_count() const;
  [[nodiscard]] std::size_t queue_depth() const;

 private:
  struct SubscriptionEntry {
    RuntimeEventSubscription subscription;
    RuntimeEventHandler handler;
  };

  [[nodiscard]] static std::int64_t default_now_ms();
  [[nodiscard]] static bool matches(
      const SubscriptionEntry& subscription,
      const RuntimeEventEnvelope& event);
  bool evict_oldest_non_audit_locked();

  RuntimeEventBusOptions options_{};
  mutable std::mutex dispatch_mutex_;
  std::deque<RuntimeEventEnvelope> pending_events_;
  std::vector<SubscriptionEntry> subscriptions_;
  std::uint64_t next_sequence_ = 1U;
  std::uint64_t next_subscription_id_ = 1U;
  std::uint64_t dropped_count_ = 0U;
  std::size_t non_audit_queue_depth_ = 0U;
};

}  // namespace dasall::runtime
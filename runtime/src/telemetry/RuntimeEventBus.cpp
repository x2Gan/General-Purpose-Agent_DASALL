#include "RuntimeEventBus.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace dasall::runtime {

RuntimeEventBus::RuntimeEventBus(RuntimeEventBusOptions options)
    : options_(std::move(options)) {
  if (!options_.now_ms) {
    options_.now_ms = []() { return default_now_ms(); };
  }
}

RuntimeEventSubscription RuntimeEventBus::subscribe(RuntimeEventHandler handler) {
  return subscribe(std::string(), std::move(handler), false);
}

RuntimeEventSubscription RuntimeEventBus::subscribe(
    std::string event_name_filter,
    RuntimeEventHandler handler,
    const bool audit_only) {
  const std::lock_guard<std::mutex> lock(dispatch_mutex_);
  const RuntimeEventSubscription subscription{
      .subscription_id = next_subscription_id_++,
      .event_name_filter = std::move(event_name_filter),
      .audit_only = audit_only,
  };
  subscriptions_.push_back(SubscriptionEntry{
      .subscription = subscription,
      .handler = std::move(handler),
  });
  return subscription;
}

bool RuntimeEventBus::unsubscribe(const std::uint64_t subscription_id) {
  const std::lock_guard<std::mutex> lock(dispatch_mutex_);
  const auto previous_size = subscriptions_.size();
  subscriptions_.erase(
      std::remove_if(
          subscriptions_.begin(),
          subscriptions_.end(),
          [subscription_id](const SubscriptionEntry& entry) {
            return entry.subscription.subscription_id == subscription_id;
          }),
      subscriptions_.end());
  return subscriptions_.size() != previous_size;
}

RuntimeEventPublishResult RuntimeEventBus::publish(RuntimeEventEnvelope event) {
  if (!event.has_minimum_fields()) {
    return RuntimeEventPublishResult{
        .accepted = false,
        .dropped_oldest = false,
        .dropped_count = drop_count(),
        .queue_depth = queue_depth(),
    };
  }

  if (event.timestamp_ms <= 0) {
    event.timestamp_ms = options_.now_ms();
  }

  const std::lock_guard<std::mutex> lock(dispatch_mutex_);
  if (!event.audit && options_.max_non_audit_queue_depth == 0U) {
    return RuntimeEventPublishResult{
        .accepted = false,
        .dropped_oldest = false,
        .dropped_count = dropped_count_,
        .queue_depth = pending_events_.size(),
    };
  }

  bool dropped_oldest = false;
  if (!event.audit && non_audit_queue_depth_ >= options_.max_non_audit_queue_depth) {
    dropped_oldest = evict_oldest_non_audit_locked();
    if (dropped_oldest) {
      ++dropped_count_;
    }
  }

  event.sequence = next_sequence_++;
  pending_events_.push_back(std::move(event));
  if (!pending_events_.back().audit) {
    ++non_audit_queue_depth_;
  }

  return RuntimeEventPublishResult{
      .accepted = true,
      .dropped_oldest = dropped_oldest,
      .dropped_count = dropped_count_,
      .queue_depth = pending_events_.size(),
  };
}

std::size_t RuntimeEventBus::dispatch_pending(const std::size_t max_events) {
  std::size_t dispatched = 0U;
  while (dispatched < max_events) {
    RuntimeEventEnvelope event;
    std::vector<SubscriptionEntry> subscribers;
    {
      const std::lock_guard<std::mutex> lock(dispatch_mutex_);
      if (pending_events_.empty()) {
        break;
      }

      event = pending_events_.front();
      pending_events_.pop_front();
      if (!event.audit && non_audit_queue_depth_ > 0U) {
        --non_audit_queue_depth_;
      }
      subscribers = subscriptions_;
    }

    for (const auto& subscriber : subscribers) {
      if (matches(subscriber, event)) {
        subscriber.handler(event);
      }
    }
    ++dispatched;
  }

  return dispatched;
}

std::vector<RuntimeEventEnvelope> RuntimeEventBus::pending_snapshot() const {
  const std::lock_guard<std::mutex> lock(dispatch_mutex_);
  return std::vector<RuntimeEventEnvelope>(pending_events_.begin(), pending_events_.end());
}

std::uint64_t RuntimeEventBus::drop_count() const {
  const std::lock_guard<std::mutex> lock(dispatch_mutex_);
  return dropped_count_;
}

std::size_t RuntimeEventBus::queue_depth() const {
  const std::lock_guard<std::mutex> lock(dispatch_mutex_);
  return pending_events_.size();
}

std::int64_t RuntimeEventBus::default_now_ms() {
  using Clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

bool RuntimeEventBus::matches(
    const SubscriptionEntry& subscription,
    const RuntimeEventEnvelope& event) {
  if (subscription.subscription.audit_only && !event.audit) {
    return false;
  }

  if (!subscription.subscription.event_name_filter.empty() &&
      subscription.subscription.event_name_filter != event.event_name) {
    return false;
  }

  return true;
}

bool RuntimeEventBus::evict_oldest_non_audit_locked() {
  const auto candidate = std::find_if(
      pending_events_.begin(),
      pending_events_.end(),
      [](const RuntimeEventEnvelope& event) { return !event.audit; });
  if (candidate == pending_events_.end()) {
    return false;
  }

  pending_events_.erase(candidate);
  if (non_audit_queue_depth_ > 0U) {
    --non_audit_queue_depth_;
  }
  return true;
}

}  // namespace dasall::runtime
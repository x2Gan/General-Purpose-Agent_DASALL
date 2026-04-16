#include "mcp/CapabilityCache.h"

#include <atomic>
#include <chrono>
#include <utility>

namespace {

[[nodiscard]] bool has_value(const std::optional<std::string>& value) {
  return value.has_value() && !value->empty();
}

[[nodiscard]] std::int64_t clamp_age_ms(std::int64_t now_ms, std::int64_t then_ms) {
  if (now_ms <= then_ms) {
    return 0;
  }
  return now_ms - then_ms;
}

}  // namespace

namespace dasall::tools::mcp {

CapabilityCache::CapabilityCache(CapabilityCacheOptions options)
    : options_(std::move(options)) {
  if (!options_.now_ms) {
    options_.now_ms = &CapabilityCache::default_now_ms;
  }
}

std::optional<CapabilitySnapshot> CapabilityCache::snapshot(
    std::string_view server_id) const {
  if (server_id.empty()) {
    return std::nullopt;
  }

  const auto current_state = std::atomic_load_explicit(&state_, std::memory_order_acquire);
  const auto found = current_state->snapshots_by_server.find(std::string(server_id));
  if (found == current_state->snapshots_by_server.end()) {
    return std::nullopt;
  }

  return materialize_snapshot(found->second, current_time_ms());
}

void CapabilityCache::update(CapabilitySnapshot snapshot) {
  if (snapshot.server_id.empty()) {
    return;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_state = std::atomic_load_explicit(&state_, std::memory_order_acquire);
  auto next_state = *current_state;

  const auto existing = next_state.snapshots_by_server.find(snapshot.server_id);
  if ((!snapshot.trust_marker.has_value() || snapshot.trust_marker->empty()) &&
      existing != next_state.snapshots_by_server.end() &&
      has_value(existing->second.trust_marker)) {
    snapshot.trust_marker = existing->second.trust_marker;
  }

  snapshot.last_refresh_at_ms = current_time_ms();
  snapshot.last_error.reset();
  snapshot.freshness = CapabilityFreshness::fresh;

  next_state.snapshots_by_server[snapshot.server_id] = std::move(snapshot);
  next_state.revision = current_state->revision + 1U;
  publish_state(&state_, std::move(next_state));
}

bool CapabilityCache::invalidate(std::string_view server_id) {
  if (server_id.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_state = std::atomic_load_explicit(&state_, std::memory_order_acquire);
  auto next_state = *current_state;
  const auto erased = next_state.snapshots_by_server.erase(std::string(server_id));
  if (erased == 0U) {
    return false;
  }

  next_state.revision = current_state->revision + 1U;
  publish_state(&state_, std::move(next_state));
  return true;
}

void CapabilityCache::mark_failed(std::string server_id, std::string error_message) {
  if (server_id.empty()) {
    return;
  }

  if (error_message.empty()) {
    error_message = "mcp.capability_refresh_failed";
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_state = std::atomic_load_explicit(&state_, std::memory_order_acquire);
  auto next_state = *current_state;
  const auto existing = next_state.snapshots_by_server.find(server_id);

  CapabilitySnapshot failed_snapshot{};
  if (existing != next_state.snapshots_by_server.end()) {
    failed_snapshot = existing->second;
  } else {
    failed_snapshot.server_id = server_id;
  }
  failed_snapshot.last_error = std::move(error_message);
  failed_snapshot.freshness =
      classify_freshness(failed_snapshot, current_time_ms());

  next_state.snapshots_by_server[failed_snapshot.server_id] = std::move(failed_snapshot);
  next_state.revision = current_state->revision + 1U;
  publish_state(&state_, std::move(next_state));
}

std::vector<CapabilitySnapshot> CapabilityCache::list_trusted() const {
  const auto current_state = std::atomic_load_explicit(&state_, std::memory_order_acquire);
  const auto now_ms = current_time_ms();

  std::vector<CapabilitySnapshot> trusted_snapshots;
  trusted_snapshots.reserve(current_state->snapshots_by_server.size());

  for (const auto& [server_id, stored_snapshot] : current_state->snapshots_by_server) {
    static_cast<void>(server_id);
    auto snapshot = materialize_snapshot(stored_snapshot, now_ms);
    if (!has_value(snapshot.trust_marker)) {
      continue;
    }
    if (snapshot.freshness == CapabilityFreshness::expired) {
      continue;
    }
    if (snapshot.freshness == CapabilityFreshness::stale && !options_.stale_read_allowed) {
      continue;
    }
    trusted_snapshots.push_back(std::move(snapshot));
  }

  return trusted_snapshots;
}

std::uint64_t CapabilityCache::revision() const {
  return std::atomic_load_explicit(&state_, std::memory_order_acquire)->revision;
}

std::int64_t CapabilityCache::default_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::int64_t CapabilityCache::current_time_ms() const {
  return options_.now_ms ? options_.now_ms() : default_now_ms();
}

CapabilitySnapshot CapabilityCache::materialize_snapshot(
    const CapabilitySnapshot& stored_snapshot,
    std::int64_t now_ms) const {
  auto snapshot = stored_snapshot;
  snapshot.freshness = classify_freshness(stored_snapshot, now_ms);
  return snapshot;
}

CapabilityFreshness CapabilityCache::classify_freshness(
    const CapabilitySnapshot& stored_snapshot,
    std::int64_t now_ms) const {
  if (!stored_snapshot.last_refresh_at_ms.has_value() || options_.expire_after_ms <= 0) {
    return CapabilityFreshness::expired;
  }

  const auto age_ms = clamp_age_ms(now_ms, *stored_snapshot.last_refresh_at_ms);
  if (age_ms >= options_.expire_after_ms) {
    return CapabilityFreshness::expired;
  }

  if (stored_snapshot.last_error.has_value()) {
    return CapabilityFreshness::stale;
  }

  return CapabilityFreshness::fresh;
}

void CapabilityCache::publish_state(
    std::shared_ptr<const CapabilityCacheState>* state_slot,
    CapabilityCacheState next_state) {
  std::atomic_store_explicit(
      state_slot,
      std::make_shared<const CapabilityCacheState>(std::move(next_state)),
      std::memory_order_release);
}

}  // namespace dasall::tools::mcp
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ICapabilityCache.h"

namespace dasall::tools::mcp {

struct CapabilityCacheOptions {
  std::int64_t expire_after_ms = 0;
  bool stale_read_allowed = false;
  std::function<std::int64_t()> now_ms;
};

struct CapabilityCacheState {
  std::map<std::string, CapabilitySnapshot> snapshots_by_server;
  std::uint64_t revision = 0U;
};

class CapabilityCache final : public ICapabilityCache {
 public:
  explicit CapabilityCache(CapabilityCacheOptions options = {});

  [[nodiscard]] std::optional<CapabilitySnapshot> snapshot(
      std::string_view server_id) const override;
  void update(CapabilitySnapshot snapshot) override;

  [[nodiscard]] bool invalidate(std::string_view server_id);
  void mark_failed(std::string server_id, std::string error_message);
  [[nodiscard]] std::vector<CapabilitySnapshot> list_trusted() const;
  [[nodiscard]] std::uint64_t revision() const;

 private:
  [[nodiscard]] static std::int64_t default_now_ms();
  [[nodiscard]] std::int64_t current_time_ms() const;
  [[nodiscard]] CapabilitySnapshot materialize_snapshot(
      const CapabilitySnapshot& stored_snapshot,
      std::int64_t now_ms) const;
  [[nodiscard]] CapabilityFreshness classify_freshness(
      const CapabilitySnapshot& stored_snapshot,
      std::int64_t now_ms) const;
  static void publish_state(
      std::shared_ptr<const CapabilityCacheState>* state_slot,
      CapabilityCacheState next_state);

  CapabilityCacheOptions options_;
  mutable std::mutex write_mutex_;
  std::shared_ptr<const CapabilityCacheState> state_ =
      std::make_shared<const CapabilityCacheState>();
};

}  // namespace dasall::tools::mcp
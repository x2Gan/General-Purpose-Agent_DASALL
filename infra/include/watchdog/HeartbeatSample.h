#pragma once

#include <cstdint>
#include <string>

namespace dasall::infra::watchdog {

struct HeartbeatSample {
  std::string entity_id;
  std::int64_t heartbeat_ts = 0;
  std::int64_t deadline_ts = 0;
  std::uint64_t seq_no = 0;

  [[nodiscard]] bool has_required_fields() const {
    return !entity_id.empty() && heartbeat_ts > 0 && deadline_ts >= heartbeat_ts &&
           seq_no > 0;
  }

  [[nodiscard]] bool is_older_than(const HeartbeatSample& other) const {
    if (seq_no != other.seq_no) {
      return seq_no < other.seq_no;
    }

    if (heartbeat_ts != other.heartbeat_ts) {
      return heartbeat_ts < other.heartbeat_ts;
    }

    return deadline_ts < other.deadline_ts;
  }

  [[nodiscard]] bool is_stale_against(const HeartbeatSample& latest) const {
    return entity_id == latest.entity_id &&
           (is_older_than(latest) ||
            (seq_no == latest.seq_no && heartbeat_ts == latest.heartbeat_ts &&
             deadline_ts == latest.deadline_ts));
  }
};

}  // namespace dasall::infra::watchdog
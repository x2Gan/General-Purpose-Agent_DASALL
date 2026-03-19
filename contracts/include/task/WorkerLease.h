#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace dasall::contracts {

// WorkerLease is the lease-metadata object introduced by WP04-T020.
// It is maintained inside the multi-agent collaboration subdomain and provides
// the minimum contract surface needed to reason about lease ownership,
// expiration, renewal window, and release semantics.
//
// Semantic boundary (WP04-T020 frozen):
//   Allowed:
//     1. Lease identity: lease_id
//     2. Worker binding: worker_ref
//     3. Hard lease deadline: deadline_at
//     4. Local renewal window: renewal_deadline_at
//     5. Early-release summary: release_reason
//
//   Forbidden:
//     - Global session / FSM control state (session_id, global_fsm_state)
//     - Top-level checkpoint / resume entry semantics (checkpoint_ref,
//       resume_token)
//     - Final result semantics (agent_result, final_agent_response,
//       merged_result)
struct WorkerLease {
  // -----------------------------------------------------------------------
  // Required fields (WP04-T020, 3 items)
  // -----------------------------------------------------------------------

  // Stable identity for the lease record itself.
  std::optional<std::string> lease_id;

  // Worker reference currently bound to the lease. This remains a compact
  // string anchor rather than a full worker descriptor object.
  std::optional<std::string> worker_ref;

  // Absolute hard deadline for the lease in milliseconds since epoch.
  std::optional<std::int64_t> deadline_at;

  // -----------------------------------------------------------------------
  // Optional fields (WP04-T020, 2 items)
  // -----------------------------------------------------------------------

  // Latest point at which a renewal request is still considered valid.
  std::optional<std::int64_t> renewal_deadline_at;

  // Early-release reason summary when the lease is relinquished before the
  // hard deadline.
  std::optional<std::string> release_reason;
};

}  // namespace dasall::contracts
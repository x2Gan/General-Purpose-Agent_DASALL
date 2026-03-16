#pragma once

namespace dasall::contracts {

// MultiAgentRequestTag is a compile-time stable marker for MultiAgentRequest.
// The type is empty by design to prevent coupling WP-01 boundary work with
// future multi-agent request field layout decisions.
struct MultiAgentRequestTag final {};

}  // namespace dasall::contracts

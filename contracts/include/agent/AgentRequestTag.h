#pragma once

namespace dasall::contracts {

// AgentRequestTag is a compile-time placeholder for the WP01 stable object
// AgentRequest. This tag intentionally carries no fields to avoid introducing
// any WP-02/WP-03 schema semantics in the WP-01 boundary phase.
struct AgentRequestTag final {};

}  // namespace dasall::contracts

#pragma once

namespace dasall::contracts {

// ReflectionDecisionTag is a compile-time marker for ReflectionDecision. No
// fields are defined so suggestion semantics can stay decoupled from execution
// structure while WP-01 enforces object presence and naming consistency.
struct ReflectionDecisionTag final {};

}  // namespace dasall::contracts

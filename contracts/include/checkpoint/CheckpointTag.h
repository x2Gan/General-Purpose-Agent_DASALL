#pragma once

namespace dasall::contracts {

// CheckpointTag marks Checkpoint as a stable contracts object. The tag remains
// empty to keep WP-01 focused on boundary identity and postpone checkpoint
// field schema evolution to downstream package scopes.
struct CheckpointTag final {};

}  // namespace dasall::contracts

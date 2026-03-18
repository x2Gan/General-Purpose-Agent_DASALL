#pragma once

namespace dasall::contracts {

// Stable type-presence tag for BeliefState (WP03-T009).
// Used by MainFlowContracts.h to occupy the chain slot without
// pulling in the full BeliefState struct definition.
struct BeliefStateTag final {};

}  // namespace dasall::contracts

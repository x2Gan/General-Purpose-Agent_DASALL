#pragma once

namespace dasall::contracts {

// ObservationTag is the stable type marker for Observation. This tag is
// intentionally fieldless so the contracts include graph can stabilize first,
// while observation payload schema remains deferred to later freeze steps.
struct ObservationTag final {};

}  // namespace dasall::contracts

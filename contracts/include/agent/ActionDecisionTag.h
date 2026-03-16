#pragma once

namespace dasall::contracts {

// ActionDecisionTag is a stable object marker for ActionDecision. The empty
// form ensures this header can be included across modules without implying any
// decision payload schema in the WP-01 contracts freeze stage.
struct ActionDecisionTag final {};

}  // namespace dasall::contracts

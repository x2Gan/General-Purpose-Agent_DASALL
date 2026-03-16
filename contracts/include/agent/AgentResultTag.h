#pragma once

namespace dasall::contracts {

// AgentResultTag marks AgentResult as a stable contracts object at compile
// time. It intentionally excludes output fields so WP-01 can enforce naming
// and boundary presence without prematurely freezing result structure details.
struct AgentResultTag final {};

}  // namespace dasall::contracts

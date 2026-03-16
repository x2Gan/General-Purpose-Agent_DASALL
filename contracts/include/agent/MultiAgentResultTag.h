#pragma once

namespace dasall::contracts {

// MultiAgentResultTag is a compile-time stable marker for MultiAgentResult.
// This placeholder keeps the contracts layer include-able while deferring all
// payload semantics to later work packages.
struct MultiAgentResultTag final {};

}  // namespace dasall::contracts

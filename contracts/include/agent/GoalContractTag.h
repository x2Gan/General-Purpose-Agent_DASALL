#pragma once

namespace dasall::contracts {

// GoalContractTag is a compile-time placeholder for GoalContract. The type is
// intentionally empty so the boundary remains object-level only and does not
// leak field-level meaning before downstream work packages are frozen.
struct GoalContractTag final {};

}  // namespace dasall::contracts

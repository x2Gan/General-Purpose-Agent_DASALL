#pragma once

namespace dasall::contracts {

// ErrorInfoTag provides a stable object-level marker for ErrorInfo. The type
// deliberately has no members so ErrorInfo field definitions can be frozen in
// later work packages without breaking WP-01 boundary invariants.
struct ErrorInfoTag final {};

}  // namespace dasall::contracts

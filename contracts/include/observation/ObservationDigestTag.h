#pragma once

namespace dasall::contracts {

// ObservationDigestTag marks ObservationDigest as a stable object in compile-
// time contracts usage. The empty shape enforces object identity only and
// avoids embedding digest structure commitments in WP-01.
struct ObservationDigestTag final {};

}  // namespace dasall::contracts

#pragma once

namespace dasall::contracts {

// WorkerTaskTag marks WorkerTask as a stable contracts object. This fieldless
// placeholder keeps WP-01 at object-boundary granularity and prevents leaking
// scheduler or worker execution semantics into the contracts layer.
struct WorkerTaskTag final {};

}  // namespace dasall::contracts

#pragma once

#include "boundary/CompatibilityGuards.h"
#include "checkpoint/CheckpointTag.h"
#include "observation/ErrorInfoTag.h"

namespace dasall::contracts {

// CrossCuttingContracts centralizes entry symbols for WP-02 horizontal
// primitives. This keeps callers on one include path while each semantic
// domain (error/event/checkpoint/id-time/enum) continues to evolve behind
// its own dedicated contract headers.
struct CrossCuttingContracts final {
  // Error entry: currently anchored to the stable ErrorInfo object marker.
  using ErrorEntry = ErrorInfoTag;

  // Event entry: EventEnvelope schema is introduced in later WP-02 tasks.
  // A dedicated marker is provided now so call sites can bind to an explicit
  // event entry symbol without depending on payload fields.
  struct EventEntry final {};

  // Checkpoint entry: stable marker for checkpoint-family contracts.
  using CheckpointEntry = CheckpointTag;

  // ID/Time compatibility entry points reuse the existing normalization model.
  using TimeFields = TimeoutFieldSet;
  using TimeNormalization = TimeoutNormalizationResult;

  // ID/Time guard entry: normalizes timeout/deadline fields into canonical ms.
  static inline TimeNormalization normalize_time_fields(const TimeFields& fields) {
    return normalize_timeout_fields(fields);
  }

  // Enum compatibility guard entry: unknown raw values downgrade to
  // Unspecified to preserve forward/backward compatibility.
  template <typename Enum>
  static inline Enum normalize_enum_with_unspecified(int raw_value,
                                                     const int* known_values,
                                                     std::size_t known_value_count,
                                                     Enum unspecified_value) {
    return fallback_unknown_enum_value<Enum>(raw_value,
                                             known_values,
                                             known_value_count,
                                             unspecified_value);
  }
};

}  // namespace dasall::contracts

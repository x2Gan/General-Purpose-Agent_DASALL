#pragma once

#include <string_view>

#include "boundary/GuardCommon.h"
#include "observation/ObservationDigest.h"
#include "observation/ObservationSource.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for ObservationDigest validation, following the same pattern
// as ObservationGuardResult (WP03-T006) and ObservationSourceGuardResult
// (WP03-T007).
// ---------------------------------------------------------------------------
struct ObservationDigestGuardResult {
  bool ok = false;
  std::string_view reason = "observation digest validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP03-T008-B).
//
// Validates that all 5 required fields are present with meaningful values:
//   R1) observation_id — present and non-empty.
//   R2) summary        — present and non-empty.
//   R3) key_facts      — present (has_value; empty vector is allowed).
//   R4) citations      — present (has_value; empty vector is allowed).
//   R5) confidence     — present and in [0.0, 1.0].
//
// Design note:
//   key_facts and citations allow empty vectors because a DigestBuilder may
//   legitimately produce a digest with no extractable facts (e.g. empty tool
//   output). The quality is reflected through the confidence field.
// ---------------------------------------------------------------------------
inline ObservationDigestGuardResult
validate_observation_digest_required_fields(const ObservationDigest& digest) {
  // R1: observation_id must be present and non-empty.
  if (!has_non_empty_value(digest.observation_id)) {
    return ObservationDigestGuardResult{
        .ok = false,
        .reason = "observation_id is required and must be non-empty",
    };
  }

  // R2: summary must be present and non-empty.
  if (!has_non_empty_value(digest.summary)) {
    return ObservationDigestGuardResult{
        .ok = false,
        .reason = "summary is required and must be non-empty",
    };
  }

  // R3: key_facts must be present (empty vector OK).
  if (!digest.key_facts.has_value()) {
    return ObservationDigestGuardResult{
        .ok = false,
        .reason = "key_facts is required",
    };
  }

  // R4: citations must be present (empty vector OK).
  if (!digest.citations.has_value()) {
    return ObservationDigestGuardResult{
        .ok = false,
        .reason = "citations is required",
    };
  }

  // R5: confidence must be present and in [0.0, 1.0].
  if (!digest.confidence.has_value()) {
    return ObservationDigestGuardResult{
        .ok = false,
        .reason = "confidence is required",
    };
  }
  if (*digest.confidence < 0.0f || *digest.confidence > 1.0f) {
    return ObservationDigestGuardResult{
        .ok = false,
        .reason = "confidence must be in [0.0, 1.0]",
    };
  }

  return ObservationDigestGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary constraint validation (WP03-T008-B).
//
// Validates semantic boundary rules on top of required fields:
//   All required-field checks (Layer 1).
//   R6) source, if present, must not be Unspecified.
//   R7) created_at, if present, must be positive.
//
// This layer ensures optional fields comply with their documented constraints
// without introducing new required-ness.
// ---------------------------------------------------------------------------
inline ObservationDigestGuardResult
validate_observation_digest_boundary(const ObservationDigest& digest) {
  // Layer 1: required-field presence.
  auto required_result = validate_observation_digest_required_fields(digest);
  if (!required_result.ok) {
    return required_result;
  }

  // R6: source, if present, must not be Unspecified (WP02-T012 sentinel).
  if (digest.source.has_value() &&
      *digest.source == ObservationSource::Unspecified) {
    return ObservationDigestGuardResult{
        .ok = false,
        .reason = "source must not be Unspecified when present",
    };
  }

  // R7: created_at, if present, must be positive (WP02-T010).
  if (digest.created_at.has_value() && *digest.created_at <= 0) {
    return ObservationDigestGuardResult{
        .ok = false,
        .reason = "created_at must be positive when present",
    };
  }

  return ObservationDigestGuardResult{
      .ok = true,
      .reason = "observation digest boundary validation passed",
  };
}

}  // namespace dasall::contracts

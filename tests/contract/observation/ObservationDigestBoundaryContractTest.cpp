// WP03-T008-B: ObservationDigest boundary contract tests.
//
// Validates the WP03-T008 ObservationDigest layering boundary enforced by:
//   - validate_observation_digest_required_fields()  (Layer 1)
//   - validate_observation_digest_boundary()          (Layer 2)
//
// Test coverage:
//   Positive: 4 scenarios proving valid ObservationDigest states.
//   Negative: 14 scenarios covering missing required fields, confidence
//             range violations, optional field boundary violations, and
//             structural integrity checks.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "observation/ObservationDigest.h"
#include "observation/ObservationDigestGuards.h"
#include "observation/ObservationSource.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ObservationDigest;
using dasall::contracts::ObservationDigestGuardResult;
using dasall::contracts::ObservationSource;
using dasall::contracts::validate_observation_digest_boundary;
using dasall::contracts::validate_observation_digest_required_fields;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal ObservationDigest with all required fields.
// ---------------------------------------------------------------------------
ObservationDigest make_valid_digest() {
  ObservationDigest digest;
  digest.observation_id = "obs-001";
  digest.summary = "File /tmp/out.txt created successfully with 42 bytes.";
  digest.key_facts =
      std::vector<std::string>{"file created: /tmp/out.txt", "size: 42 bytes"};
  digest.citations =
      std::vector<std::string>{"observation:obs-001:payload.result"};
  digest.confidence = 0.92f;
  return digest;
}

// ===========================================================================
// Positive cases
// ===========================================================================

// P1: Minimal valid digest (required fields only) passes Layer 1.
void test_minimal_digest_passes_required_fields() {
  auto digest = make_valid_digest();
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(result.ok,
              "minimal valid digest should pass required fields guard");
}

// P2: Minimal valid digest passes Layer 2 boundary.
void test_minimal_digest_passes_boundary() {
  auto digest = make_valid_digest();
  auto result = validate_observation_digest_boundary(digest);
  assert_true(result.ok,
              "minimal valid digest should pass boundary guard");
}

// P3: Full digest with all optional fields set.
void test_full_digest_passes_boundary() {
  auto digest = make_valid_digest();
  digest.omitted_details =
      std::vector<std::string>{"raw binary content (1024 bytes)"};
  digest.source = ObservationSource::ToolExecution;
  digest.created_at = 1710000001000;
  digest.tags = std::vector<std::string>{"file-io", "write"};

  auto result = validate_observation_digest_boundary(digest);
  assert_true(result.ok,
              "full digest with all optional fields should pass boundary");
}

// P4: Digest with empty key_facts and citations vectors (valid edge case).
void test_empty_vectors_pass() {
  auto digest = make_valid_digest();
  digest.key_facts = std::vector<std::string>{};
  digest.citations = std::vector<std::string>{};
  digest.confidence = 0.1f;  // low confidence reflects empty extraction

  auto result = validate_observation_digest_boundary(digest);
  assert_true(result.ok,
              "digest with empty key_facts and citations vectors should pass");
}

// ===========================================================================
// Negative cases: missing required fields
// ===========================================================================

// N1: Missing observation_id.
void test_missing_observation_id_fails() {
  auto digest = make_valid_digest();
  digest.observation_id = std::nullopt;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "missing observation_id should fail");
}

// N2: Empty observation_id.
void test_empty_observation_id_fails() {
  auto digest = make_valid_digest();
  digest.observation_id = "";
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "empty observation_id should fail");
}

// N3: Missing summary.
void test_missing_summary_fails() {
  auto digest = make_valid_digest();
  digest.summary = std::nullopt;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "missing summary should fail");
}

// N4: Empty summary.
void test_empty_summary_fails() {
  auto digest = make_valid_digest();
  digest.summary = "";
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "empty summary should fail");
}

// N5: Missing key_facts.
void test_missing_key_facts_fails() {
  auto digest = make_valid_digest();
  digest.key_facts = std::nullopt;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "missing key_facts should fail");
}

// N6: Missing citations.
void test_missing_citations_fails() {
  auto digest = make_valid_digest();
  digest.citations = std::nullopt;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "missing citations should fail");
}

// N7: Missing confidence.
void test_missing_confidence_fails() {
  auto digest = make_valid_digest();
  digest.confidence = std::nullopt;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "missing confidence should fail");
}

// ===========================================================================
// Negative cases: confidence range violations
// ===========================================================================

// N8: Confidence below 0.0.
void test_confidence_below_zero_fails() {
  auto digest = make_valid_digest();
  digest.confidence = -0.1f;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "confidence below 0.0 should fail");
}

// N9: Confidence above 1.0.
void test_confidence_above_one_fails() {
  auto digest = make_valid_digest();
  digest.confidence = 1.1f;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(!result.ok, "confidence above 1.0 should fail");
}

// N10: Confidence exactly 0.0 should pass (boundary).
void test_confidence_zero_passes() {
  auto digest = make_valid_digest();
  digest.confidence = 0.0f;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(result.ok, "confidence exactly 0.0 should pass");
}

// N11: Confidence exactly 1.0 should pass (boundary).
void test_confidence_one_passes() {
  auto digest = make_valid_digest();
  digest.confidence = 1.0f;
  auto result = validate_observation_digest_required_fields(digest);
  assert_true(result.ok, "confidence exactly 1.0 should pass");
}

// ===========================================================================
// Negative cases: optional field boundary violations
// ===========================================================================

// N12: Source = Unspecified when present.
void test_unspecified_source_fails() {
  auto digest = make_valid_digest();
  digest.source = ObservationSource::Unspecified;
  auto result = validate_observation_digest_boundary(digest);
  assert_true(!result.ok, "Unspecified source should fail boundary");
}

// N13: created_at = 0 when present.
void test_zero_created_at_fails() {
  auto digest = make_valid_digest();
  digest.created_at = 0;
  auto result = validate_observation_digest_boundary(digest);
  assert_true(!result.ok, "zero created_at should fail boundary");
}

// N14: created_at negative when present.
void test_negative_created_at_fails() {
  auto digest = make_valid_digest();
  digest.created_at = -1000;
  auto result = validate_observation_digest_boundary(digest);
  assert_true(!result.ok, "negative created_at should fail boundary");
}

}  // namespace

int main() {
  try {
    // Positive cases (4)
    test_minimal_digest_passes_required_fields();
    test_minimal_digest_passes_boundary();
    test_full_digest_passes_boundary();
    test_empty_vectors_pass();

    // Negative cases: missing required fields (7)
    test_missing_observation_id_fails();
    test_empty_observation_id_fails();
    test_missing_summary_fails();
    test_empty_summary_fails();
    test_missing_key_facts_fails();
    test_missing_citations_fails();
    test_missing_confidence_fails();

    // Negative cases: confidence range (4, including 2 boundary passes)
    test_confidence_below_zero_fails();
    test_confidence_above_one_fails();
    test_confidence_zero_passes();
    test_confidence_one_passes();

    // Negative cases: optional field boundary violations (3)
    test_unspecified_source_fails();
    test_zero_created_at_fails();
    test_negative_created_at_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

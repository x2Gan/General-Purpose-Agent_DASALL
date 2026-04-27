#include <exception>
#include <iostream>

#include "belief/BeliefUpdateSynthesizer.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::belief::BeliefDeltaKind;
using dasall::cognition::belief::BeliefUpdateHint;
using dasall::cognition::belief::BeliefUpdateSynthesizer;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_normalize_evidence_refs_deduplicates_and_removes_known_missing_refs() {
  BeliefUpdateSynthesizer synthesizer;
  BeliefUpdateHint hint;

  hint.evidence_refs_delta = {
      {.evidence_ref = "obs-018-1", .delta_kind = BeliefDeltaKind::Upsert},
      {.evidence_ref = "", .delta_kind = BeliefDeltaKind::Upsert},
      {.evidence_ref = "obs-018-1", .delta_kind = BeliefDeltaKind::Upsert},
      {.evidence_ref = "doc-1", .delta_kind = BeliefDeltaKind::Upsert},
      {.evidence_ref = "doc-1", .delta_kind = BeliefDeltaKind::Retract},
  };
  hint.missing_evidence_refs = {
      "doc-1",
      "",
      "ref-need-1",
      "ref-need-1",
      "obs-018-1",
  };

  synthesizer.normalize_evidence_refs(hint);

  assert_equal(2, static_cast<int>(hint.evidence_refs_delta.size()),
               "normalize_evidence_refs should keep only unique non-empty evidence refs");
  assert_true(hint.evidence_refs_delta[0].evidence_ref == "obs-018-1",
              "normalize_evidence_refs should preserve first seen evidence order");
  assert_true(hint.evidence_refs_delta[1].evidence_ref == "doc-1" &&
                  hint.evidence_refs_delta[1].delta_kind == BeliefDeltaKind::Retract,
              "duplicate evidence refs should preserve the strongest retract semantics");
  assert_equal(1, static_cast<int>(hint.missing_evidence_refs.size()),
               "missing evidence refs should drop duplicates, empties, and known evidence refs");
  assert_true(hint.missing_evidence_refs.front() == "ref-need-1",
              "only unresolved missing evidence refs should remain after normalization");
}

}  // namespace

int main() {
  try {
    test_normalize_evidence_refs_deduplicates_and_removes_known_missing_refs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
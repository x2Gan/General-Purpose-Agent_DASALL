// INT-TODO-008-B: RetrievalEvidenceRef additive contract tests.

#include <exception>
#include <iostream>

#include "context/RetrievalEvidenceRef.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::RetrievalEvidenceRef;
using dasall::tests::support::assert_true;

RetrievalEvidenceRef make_valid_ref() {
  return RetrievalEvidenceRef{
      .evidence_ref = "evidence-ref-001",
      .source_ref = "doc:ci-pipeline-spec-v2",
      .source_kind = "file",
      .summary_text = "CI pipeline specification summary",
      .trust_level = "trusted",
      .freshness = "Fresh",
      .anchor_locator = "section:gate-int-02",
  };
}

void test_p1_valid_ref_with_anchor() {
  const auto ref = make_valid_ref();
  assert_true(ref.has_consistent_values(),
              "P1: valid retrieval evidence ref must be consistent");
}

void test_p2_valid_ref_without_anchor() {
  auto ref = make_valid_ref();
  ref.anchor_locator = std::nullopt;
  assert_true(ref.has_consistent_values(),
              "P2: anchor_locator may be absent");
}

void test_n1_default_constructed_ref_is_inconsistent() {
  RetrievalEvidenceRef ref;
  assert_true(!ref.has_consistent_values(),
              "N1: default constructed ref must be inconsistent");
}

void test_n2_empty_summary_text_is_inconsistent() {
  auto ref = make_valid_ref();
  ref.summary_text.clear();
  assert_true(!ref.has_consistent_values(),
              "N2: empty summary_text must be inconsistent");
}

void test_n3_empty_anchor_locator_is_inconsistent() {
  auto ref = make_valid_ref();
  ref.anchor_locator = "";
  assert_true(!ref.has_consistent_values(),
              "N3: empty anchor_locator string must be inconsistent");
}

int run_all_tests() {
  int passed = 0;
  int failed = 0;

  auto run = [&](void (*fn)(), const char* name) {
    try {
      fn();
      ++passed;
    } catch (const std::exception& e) {
      std::cerr << "FAIL [" << name << "]: " << e.what() << "\n";
      ++failed;
    }
  };

  run(test_p1_valid_ref_with_anchor, "P1_valid_ref_with_anchor");
  run(test_p2_valid_ref_without_anchor, "P2_valid_ref_without_anchor");
  run(test_n1_default_constructed_ref_is_inconsistent,
      "N1_default_constructed_ref_is_inconsistent");
  run(test_n2_empty_summary_text_is_inconsistent,
      "N2_empty_summary_text_is_inconsistent");
  run(test_n3_empty_anchor_locator_is_inconsistent,
      "N3_empty_anchor_locator_is_inconsistent");

  std::cout << "RetrievalEvidenceRefContractTest: " << passed << " passed, "
            << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main() {
  return run_all_tests();
}
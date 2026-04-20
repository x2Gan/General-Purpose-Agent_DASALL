#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "conflict/MemoryConflictResolver.h"
#include "memory/Session.h"
#include "support/TestAssertions.h"

namespace {

void seed_session(dasall::tests::mocks::FakeMemoryStore& store,
                  const std::string& session_id) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.user_id = "user-020";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = 1000;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed fact conflict session");
  }
}

void insert_fact(dasall::tests::mocks::FakeMemoryStore& store,
                 const std::string& session_id,
                 const std::string& fact_id,
                 const std::string& fact_text,
                 std::uint32_t confidence,
                 const std::string& fact_type) {
  dasall::contracts::MemoryFact fact;
  fact.fact_id = fact_id;
  fact.session_id = session_id;
  fact.fact_text = fact_text;
  fact.source_turn_ids = std::vector<std::string>{"turn-020-seed"};
  fact.confidence_score = confidence;
  fact.created_at = 1000;
  fact.fact_type = fact_type;
  if (!store.insert_fact(fact).ok) {
    throw std::runtime_error("failed to seed fact conflict fact");
  }
}

[[nodiscard]] dasall::memory::FactCandidate make_candidate(
    const std::string& session_id,
    const std::string& fact_id,
    const std::string& fact_text,
    std::uint32_t confidence,
    const std::string& fact_type) {
  dasall::memory::FactCandidate candidate;
  candidate.fact.fact_id = fact_id;
  candidate.fact.session_id = session_id;
  candidate.fact.fact_text = fact_text;
  candidate.fact.source_turn_ids = std::vector<std::string>{"turn-020-new"};
  candidate.fact.confidence_score = confidence;
  candidate.fact.created_at = 2000;
  candidate.fact.fact_type = fact_type;
  candidate.extraction_source = "observation";
  return candidate;
}

void test_resolver_keeps_related_but_different_fact_types_in_coexist_plan() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-020-coexist");
  insert_fact(store, "session-020-coexist", "fact-020-existing", "theme is dark",
              72, "preference");
  dasall::memory::MemoryConflictResolver resolver(store);

  const auto plan = resolver.resolve(
      make_candidate("session-020-coexist", "fact-020-new", "theme is dark", 68,
                     "policy"),
      "session-020-coexist");

  assert_true(plan.action == dasall::memory::ConflictAction::Coexist,
              "resolver should keep related facts in a coexist plan when their fact types differ");
  assert_true(plan.conflict_records.size() == 1U,
              "resolver should emit one coexist record for the related fact");
  assert_true(plan.conflict_records.front().action ==
                  dasall::memory::ConflictAction::Coexist,
              "resolver should mark the related-but-compatible record as Coexist");
}

void test_resolver_detects_numeric_conflict_for_same_fact_type() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-020-numeric");
  insert_fact(store, "session-020-numeric", "fact-020-existing", "retry budget 1",
              60, "quota");
  dasall::memory::MemoryConflictResolver resolver(store);

  const auto plan = resolver.resolve(
      make_candidate("session-020-numeric", "fact-020-new", "retry budget 3", 80,
                     "quota"),
      "session-020-numeric");

  assert_true(plan.action == dasall::memory::ConflictAction::Supersede,
              "resolver should treat mismatched numeric facts of the same type as a supersede-worthy conflict");
  assert_true(plan.supersede_target_id ==
                  std::optional<std::string>{"fact-020-existing"},
              "resolver should nominate the conflicting numeric fact as the supersede target");
  assert_true(plan.conflict_records.size() == 1U &&
                  plan.conflict_records.front().confidence_delta == 20,
              "resolver should expose the numeric conflict through a supersede record with the correct delta");
}

}  // namespace

int main() {
  try {
    test_resolver_keeps_related_but_different_fact_types_in_coexist_plan();
    test_resolver_detects_numeric_conflict_for_same_fact_type();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
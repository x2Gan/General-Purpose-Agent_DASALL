#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

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
    throw std::runtime_error("failed to seed conflict resolver session");
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
    throw std::runtime_error("failed to seed existing fact");
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
  candidate.extraction_source = "turn";
  return candidate;
}

void test_resolver_accepts_when_no_related_fact_exists() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-020-accept");
  dasall::memory::MemoryConflictResolver resolver(store);

  const auto plan = resolver.resolve(
      make_candidate("session-020-accept", "fact-020-new", "network mode enabled",
                     85, "status"),
      "session-020-accept");

  assert_true(plan.action == dasall::memory::ConflictAction::Accept,
              "resolver should accept a fact when no related existing fact is found");
  assert_true(plan.conflict_records.empty(),
              "resolver should not emit conflict records on the accept path");
  assert_true(!plan.supersede_target_id.has_value(),
              "resolver should not nominate a supersede target on the accept path");
}

void test_resolver_supersedes_lower_conflicting_fact() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-020-supersede");
  insert_fact(store, "session-020-supersede", "fact-020-old", "network mode enabled",
              70, "status");
  dasall::memory::MemoryConflictResolver resolver(store);

  const auto plan = resolver.resolve(
      make_candidate("session-020-supersede", "fact-020-new", "network mode disabled",
                     92, "status"),
      "session-020-supersede");

  assert_true(plan.action == dasall::memory::ConflictAction::Supersede,
              "resolver should supersede a conflicting fact when the new confidence is higher");
  assert_true(plan.supersede_target_id == std::optional<std::string>{"fact-020-old"},
              "resolver should point supersede_target_id to the highest-confidence conflicting fact");
  assert_true(plan.conflict_records.size() == 1U,
              "resolver should emit one conflict record for the supersede path");
  assert_true(plan.conflict_records.front().action ==
                  dasall::memory::ConflictAction::Supersede,
              "resolver should mark the conflict record as Supersede");
  assert_true(plan.conflict_records.front().confidence_delta == 22,
              "resolver should expose the confidence delta in the supersede record");
}

void test_resolver_rejects_lower_confidence_conflicting_fact() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-020-reject");
  insert_fact(store, "session-020-reject", "fact-020-old", "network mode enabled",
              95, "status");
  dasall::memory::MemoryConflictResolver resolver(store);

  const auto plan = resolver.resolve(
      make_candidate("session-020-reject", "fact-020-new", "network mode disabled",
                     75, "status"),
      "session-020-reject");

  assert_true(plan.action == dasall::memory::ConflictAction::Reject,
              "resolver should reject a conflicting fact when the new confidence does not exceed the existing fact");
  assert_true(!plan.supersede_target_id.has_value(),
              "resolver should not nominate a supersede target on the reject path");
  assert_true(plan.conflict_records.size() == 1U,
              "resolver should emit one conflict record for the reject path");
  assert_true(plan.conflict_records.front().action ==
                  dasall::memory::ConflictAction::Reject,
              "resolver should mark the conflict record as Reject");
  assert_true(plan.conflict_records.front().confidence_delta == -20,
              "resolver should expose a negative confidence delta when the candidate loses");
}

}  // namespace

int main() {
  try {
    test_resolver_accepts_when_no_related_fact_exists();
    test_resolver_supersedes_lower_conflicting_fact();
    test_resolver_rejects_lower_confidence_conflicting_fact();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
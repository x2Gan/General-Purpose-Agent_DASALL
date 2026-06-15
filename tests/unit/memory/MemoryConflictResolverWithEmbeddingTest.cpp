#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FakeMemoryStore.h"
#include "conflict/MemoryConflictResolver.h"
#include "memory/Session.h"
#include "support/TestAssertions.h"
#include "vector/IEmbeddingAdapter.h"

namespace {

class ScriptedEmbeddingAdapter final : public dasall::memory::IEmbeddingAdapter {
 public:
  void set_embedding(const std::string& text,
                     std::vector<float> embedding) {
    embeddings_[text] = std::move(embedding);
  }

  [[nodiscard]] std::vector<float> embed(const std::string& text) const override {
    const auto iterator = embeddings_.find(text);
    if (iterator == embeddings_.end()) {
      return {};
    }

    return iterator->second;
  }

  [[nodiscard]] int dimension() const override {
    return 3;
  }

 private:
  std::unordered_map<std::string, std::vector<float>> embeddings_;
};

void seed_session(dasall::tests::mocks::FakeMemoryStore& store,
                  const std::string& session_id) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.user_id = "user-009";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = 1000;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed embedding conflict session");
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
  fact.source_turn_ids = std::vector<std::string>{"turn-009-seed"};
  fact.confidence_score = confidence;
  fact.created_at = 1000;
  fact.fact_type = fact_type;
  if (!store.insert_fact(fact).ok) {
    throw std::runtime_error("failed to seed embedding conflict fact");
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
  candidate.fact.source_turn_ids = std::vector<std::string>{"turn-009-new"};
  candidate.fact.confidence_score = confidence;
  candidate.fact.created_at = 2000;
  candidate.fact.fact_type = fact_type;
  candidate.extraction_source = "turn";
  return candidate;
}

void test_resolver_supersedes_cross_language_restatement_when_similarity_is_high() {
  using dasall::tests::support::assert_true;

  constexpr auto kExistingText = "guest wifi requires visitor passcode";
  constexpr auto kCandidateText = "guest wifi 需要访客口令";

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-009-supersede");
  insert_fact(store,
              "session-009-supersede",
              "fact-009-existing",
              kExistingText,
              72,
              "policy");

  ScriptedEmbeddingAdapter embedding_adapter;
  embedding_adapter.set_embedding(kExistingText, {1.0F, 0.0F, 0.0F});
  embedding_adapter.set_embedding(kCandidateText, {0.97F, 0.24F, 0.0F});

  dasall::memory::MemoryConflictResolver resolver(
      store,
      dasall::memory::ConflictConfig{.embedding_similarity_threshold = 0.9},
      &embedding_adapter);

  const auto plan = resolver.resolve(
      make_candidate("session-009-supersede",
                     "fact-009-new",
                     kCandidateText,
                     91,
                     "policy"),
      "session-009-supersede");

  assert_true(plan.action == dasall::memory::ConflictAction::Supersede,
              "resolver should supersede a cross-language restatement when embedding similarity exceeds the threshold");
  assert_true(plan.supersede_target_id == std::optional<std::string>{"fact-009-existing"},
              "resolver should target the existing fact when embedding similarity upgrades coexist to supersede");
  assert_true(plan.conflict_records.size() == 1U &&
                  plan.conflict_records.front().action ==
                      dasall::memory::ConflictAction::Supersede,
              "resolver should emit a supersede conflict record for the high-similarity restatement");
}

void test_resolver_keeps_coexist_when_similarity_stays_below_threshold() {
  using dasall::tests::support::assert_true;

  constexpr auto kExistingText = "guest wifi requires visitor passcode";
  constexpr auto kCandidateText = "guest wifi 访客模式仅限白天";

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-009-coexist");
  insert_fact(store,
              "session-009-coexist",
              "fact-009-existing",
              kExistingText,
              72,
              "policy");

  ScriptedEmbeddingAdapter embedding_adapter;
  embedding_adapter.set_embedding(kExistingText, {1.0F, 0.0F, 0.0F});
  embedding_adapter.set_embedding(kCandidateText, {0.35F, 0.94F, 0.0F});

  dasall::memory::MemoryConflictResolver resolver(
      store,
      dasall::memory::ConflictConfig{.embedding_similarity_threshold = 0.85},
      &embedding_adapter);

  const auto plan = resolver.resolve(
      make_candidate("session-009-coexist",
                     "fact-009-new",
                     kCandidateText,
                     95,
                     "policy"),
      "session-009-coexist");

  assert_true(plan.action == dasall::memory::ConflictAction::Coexist,
              "resolver should keep coexist when related facts stay below the embedding similarity threshold");
  assert_true(!plan.supersede_target_id.has_value(),
              "resolver should not nominate a supersede target when embedding similarity is insufficient");
  assert_true(plan.conflict_records.size() == 1U &&
                  plan.conflict_records.front().action ==
                      dasall::memory::ConflictAction::Coexist,
              "resolver should emit a coexist record for the low-similarity related fact");
}

}  // namespace

int main() {
  try {
    test_resolver_supersedes_cross_language_restatement_when_similarity_is_high();
    test_resolver_keeps_coexist_when_similarity_stays_below_threshold();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
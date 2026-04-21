#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "conflict/MemoryConflictResolver.h"
#include "memory/Session.h"
#include "support/TestAssertions.h"

namespace {

class ThrowingFactQueryStore final : public dasall::memory::IMemoryStore {
 public:
  [[nodiscard]] std::optional<dasall::contracts::ResultCode> open(
      const dasall::memory::MemoryConfig& config) override {
    return delegate_.open(config);
  }

  void close() noexcept override {
    delegate_.close();
  }

  [[nodiscard]] std::unique_ptr<dasall::memory::IStoreTransaction> begin_immediate()
      override {
    return delegate_.begin_immediate();
  }

  [[nodiscard]] dasall::memory::SessionLoadBundle load_session_bundle(
      const dasall::memory::SessionLoadRequest& request) const override {
    return delegate_.load_session_bundle(request);
  }

  [[nodiscard]] dasall::memory::StoreResult create_session(
      const dasall::contracts::Session& session) override {
    return delegate_.create_session(session);
  }

  [[nodiscard]] dasall::memory::StoreResult append_turn(
      const dasall::contracts::Turn& turn) override {
    return delegate_.append_turn(turn);
  }

  [[nodiscard]] dasall::memory::StoreResult update_session_active(
      const std::string& session_id,
      std::int64_t last_active_at) override {
    return delegate_.update_session_active(session_id, last_active_at);
  }

  [[nodiscard]] dasall::memory::StoreResult upsert_summary(
      const dasall::contracts::SummaryMemory& summary) override {
    return delegate_.upsert_summary(summary);
  }

  [[nodiscard]] std::optional<dasall::contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id) const override {
    return delegate_.load_latest_summary(session_id);
  }

  [[nodiscard]] dasall::memory::FactQueryResult query_facts(
      const dasall::memory::FactQuery& query) const override {
    (void)query;
    throw std::runtime_error("fact query unavailable");
  }

  [[nodiscard]] dasall::memory::StoreResult insert_fact(
      const dasall::contracts::MemoryFact& fact) override {
    return delegate_.insert_fact(fact);
  }

  [[nodiscard]] dasall::memory::StoreResult supersede_fact(
      const std::string& old_fact_id,
      const std::string& new_fact_id) override {
    return delegate_.supersede_fact(old_fact_id, new_fact_id);
  }

  [[nodiscard]] dasall::memory::ExperienceQueryResult query_experiences(
      const dasall::memory::ExperienceQuery& query) const override {
    return delegate_.query_experiences(query);
  }

  [[nodiscard]] dasall::memory::StoreResult insert_experience(
      const dasall::contracts::ExperienceMemory& experience) override {
    return delegate_.insert_experience(experience);
  }

  [[nodiscard]] std::int64_t count_turns(const std::string& session_id) const override {
    return delegate_.count_turns(session_id);
  }

  [[nodiscard]] dasall::memory::StoreResult quarantine_record(
      const std::string& object_type,
      const std::string& object_id,
      const std::string& reason) override {
    return delegate_.quarantine_record(object_type, object_id, reason);
  }

  void run_wal_checkpoint(const dasall::memory::MemoryConfig& config,
                          dasall::memory::MaintenanceReport& report) override {
    delegate_.run_wal_checkpoint(config, report);
  }

  [[nodiscard]] int run_turn_retention(
      const dasall::memory::MemoryConfig& config,
      dasall::memory::MaintenanceReport& report) override {
    return delegate_.run_turn_retention(config, report);
  }

  [[nodiscard]] int run_fact_retention(
      const dasall::memory::MemoryConfig& config,
      dasall::memory::MaintenanceReport& report) override {
    return delegate_.run_fact_retention(config, report);
  }

  [[nodiscard]] int run_experience_retention(
      const dasall::memory::MemoryConfig& config,
      dasall::memory::MaintenanceReport& report) override {
    return delegate_.run_experience_retention(config, report);
  }

  [[nodiscard]] int run_quarantine_cleanup(
      const dasall::memory::MemoryConfig& config,
      dasall::memory::MaintenanceReport& report) override {
    return delegate_.run_quarantine_cleanup(config, report);
  }

 private:
  dasall::tests::mocks::FakeMemoryStore delegate_;
};

[[nodiscard]] dasall::memory::FactCandidate make_candidate(
    const std::string& session_id) {
  dasall::memory::FactCandidate candidate;
  candidate.fact.fact_id = "fact-020-degraded";
  candidate.fact.session_id = session_id;
  candidate.fact.fact_text = "network mode disabled";
  candidate.fact.source_turn_ids = std::vector<std::string>{"turn-020-degraded"};
  candidate.fact.confidence_score = 88;
  candidate.fact.created_at = 2000;
  candidate.fact.fact_type = "status";
  candidate.extraction_source = "reflection";
  return candidate;
}

void seed_session(ThrowingFactQueryStore& store,
                  const std::string& session_id) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.user_id = "user-020";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = 1000;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed degraded conflict session");
  }
}

void test_resolver_degrades_to_accept_when_fact_query_throws() {
  using dasall::tests::support::assert_true;

  ThrowingFactQueryStore store;
  seed_session(store, "session-020-degraded");
  dasall::memory::MemoryConflictResolver resolver(store);

  const auto plan = resolver.resolve(make_candidate("session-020-degraded"),
                                     "session-020-degraded");

  assert_true(plan.action == dasall::memory::ConflictAction::Accept,
              "resolver should degrade to Accept when related fact lookup throws");
  assert_true(plan.degraded,
              "resolver should mark the plan as degraded when conflict checks are skipped");
  assert_true(plan.conflict_records.empty(),
              "resolver should not emit conflict records when the check is skipped");
  assert_true(plan.warnings.size() == 1U &&
                  plan.warnings.front() == "conflict_check_skipped",
              "resolver should surface conflict_check_skipped on the degraded path");
}

}  // namespace

int main() {
  try {
    test_resolver_degrades_to_accept_when_fact_query_throws();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
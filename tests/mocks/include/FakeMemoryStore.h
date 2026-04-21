#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IMemoryStore.h"

namespace dasall::tests::mocks {

class FakeMemoryStore final : public memory::IMemoryStore {
 public:
  FakeMemoryStore() = default;

  [[nodiscard]] std::optional<contracts::ResultCode> open(
      const memory::MemoryConfig& config) override {
    last_open_backend_ = config.storage.backend;
    is_open_ = true;
    return std::nullopt;
  }

  void close() noexcept override {
    is_open_ = false;
    active_transaction_ = false;
  }

  [[nodiscard]] std::unique_ptr<memory::IStoreTransaction> begin_immediate() override {
    active_transaction_ = true;
    return std::make_unique<FakeStoreTransaction>(*this);
  }

  [[nodiscard]] memory::SessionLoadBundle load_session_bundle(
      const memory::SessionLoadRequest& request) const override {
    memory::SessionLoadBundle bundle;

    const auto session_it = sessions_.find(request.session_id);
    if (session_it != sessions_.end()) {
      bundle.session = session_it->second;
    }

    const auto turns_it = turns_by_session_.find(request.session_id);
    if (turns_it != turns_by_session_.end()) {
      bundle.total_turn_count = static_cast<int>(turns_it->second.size());

      for (auto it = turns_it->second.rbegin(); it != turns_it->second.rend(); ++it) {
        if (request.recent_turn_limit > 0 &&
            static_cast<int>(bundle.recent_turns.size()) >= request.recent_turn_limit) {
          break;
        }
        bundle.recent_turns.push_back(*it);
      }
    }

    return bundle;
  }

  [[nodiscard]] memory::StoreResult create_session(
      const contracts::Session& session) override {
    const auto session_id = required_id(session.session_id);
    if (!session_id.has_value()) {
      return memory::StoreResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string{"session_id is required"});
    }

    sessions_[*session_id] = session;
    turns_by_session_.try_emplace(*session_id);
    return memory::StoreResult::success(*session_id);
  }

  [[nodiscard]] memory::StoreResult append_turn(const contracts::Turn& turn) override {
    const auto session_id = required_id(turn.session_id);
    const auto turn_id = required_id(turn.turn_id);
    if (!session_id.has_value() || !turn_id.has_value()) {
      return memory::StoreResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string{"turn_id and session_id are required"});
    }

    turns_by_session_[*session_id].push_back(turn);

    auto session_it = sessions_.find(*session_id);
    if (session_it != sessions_.end()) {
      if (!session_it->second.turn_ids.has_value()) {
        session_it->second.turn_ids = std::vector<std::string>{};
      }
      session_it->second.turn_ids->push_back(*turn_id);
    }

    return memory::StoreResult::success(*turn_id);
  }

  [[nodiscard]] memory::StoreResult update_session_active(
      const std::string& session_id, std::int64_t last_active_at) override {
    auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
      return memory::StoreResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string{"session not found"});
    }

    session_it->second.last_active_at = last_active_at;
    return memory::StoreResult::success(session_id);
  }

  [[nodiscard]] memory::StoreResult upsert_summary(
      const contracts::SummaryMemory& summary) override {
    const auto session_id = required_id(summary.session_id);
    const auto summary_id = required_id(summary.summary_id);
    if (!session_id.has_value() || !summary_id.has_value()) {
      return memory::StoreResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string{"summary_id and session_id are required"});
    }

    summaries_by_session_[*session_id] = summary;

    auto session_it = sessions_.find(*session_id);
    if (session_it != sessions_.end()) {
      session_it->second.latest_summary_memory_ref = *summary_id;
    }

    return memory::StoreResult::success(*summary_id);
  }

  [[nodiscard]] std::optional<contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id) const override {
    const auto summary_it = summaries_by_session_.find(session_id);
    if (summary_it == summaries_by_session_.end()) {
      return std::nullopt;
    }

    return summary_it->second;
  }

  [[nodiscard]] memory::FactQueryResult query_facts(
      const memory::FactQuery& query) const override {
    memory::FactQueryResult result;

    for (const auto& [fact_id, fact] : facts_by_id_) {
      (void)fact_id;

      if (query.session_id.has_value() && fact.session_id != query.session_id) {
        continue;
      }

      if (query.user_id.has_value() && !session_matches_user(fact.session_id, *query.user_id)) {
        continue;
      }

      if (query.fact_type.has_value() && fact.fact_type != query.fact_type) {
        continue;
      }

      if (query.min_confidence.has_value()) {
        if (!fact.confidence_score.has_value() ||
            static_cast<int>(*fact.confidence_score) < *query.min_confidence) {
          continue;
        }
      }

      if (query.exclude_superseded && fact.superseded_by_fact_id.has_value()) {
        continue;
      }

      result.facts.push_back(fact);
      if (query.limit > 0 && static_cast<int>(result.facts.size()) >= query.limit) {
        break;
      }
    }

    result.total_count = static_cast<int>(result.facts.size());
    return result;
  }

  [[nodiscard]] memory::StoreResult insert_fact(
      const contracts::MemoryFact& fact) override {
    const auto fact_id = required_id(fact.fact_id);
    if (!fact_id.has_value()) {
      return memory::StoreResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string{"fact_id is required"});
    }

    facts_by_id_[*fact_id] = fact;
    return memory::StoreResult::success(*fact_id);
  }

  [[nodiscard]] memory::StoreResult supersede_fact(
      const std::string& old_fact_id, const std::string& new_fact_id) override {
    auto fact_it = facts_by_id_.find(old_fact_id);
    if (fact_it == facts_by_id_.end()) {
      return memory::StoreResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string{"old fact not found"});
    }

    fact_it->second.superseded_by_fact_id = new_fact_id;
    return memory::StoreResult::success(old_fact_id);
  }

  [[nodiscard]] memory::ExperienceQueryResult query_experiences(
      const memory::ExperienceQuery& query) const override {
    memory::ExperienceQueryResult result;

    for (const auto& [experience_id, experience] : experiences_by_id_) {
      (void)experience_id;

      if (query.session_id.has_value() && experience.session_id != query.session_id) {
        continue;
      }

      if (query.user_id.has_value() &&
          !session_matches_user(experience.session_id, *query.user_id)) {
        continue;
      }

      if (query.stage.has_value() && !matches_stage(experience, *query.stage)) {
        continue;
      }

      if (query.applicable_domains.has_value() &&
          !matches_any_domain(experience.applicable_domains, *query.applicable_domains)) {
        continue;
      }

      if (query.exclude_expired && experience.expires_at.has_value() && *experience.expires_at <= 0) {
        continue;
      }

      result.experiences.push_back(experience);
      if (query.limit > 0 && static_cast<int>(result.experiences.size()) >= query.limit) {
        break;
      }
    }

    result.total_count = static_cast<int>(result.experiences.size());
    return result;
  }

  [[nodiscard]] memory::StoreResult insert_experience(
      const contracts::ExperienceMemory& experience) override {
    const auto experience_id = required_id(experience.experience_id);
    if (!experience_id.has_value()) {
      return memory::StoreResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string{"experience_id is required"});
    }

    experiences_by_id_[*experience_id] = experience;
    return memory::StoreResult::success(*experience_id);
  }

  [[nodiscard]] std::int64_t count_turns(const std::string& session_id) const override {
    const auto turns_it = turns_by_session_.find(session_id);
    if (turns_it == turns_by_session_.end()) {
      return 0;
    }

    return static_cast<std::int64_t>(turns_it->second.size());
  }

  [[nodiscard]] memory::StoreResult quarantine_record(
      const std::string& object_type,
      const std::string& object_id,
      const std::string& reason) override {
    quarantine_records_.push_back(QuarantineRecord{
        .object_type = object_type,
        .object_id = object_id,
        .reason = reason,
    });
    return memory::StoreResult::success(object_id);
  }

  void run_wal_checkpoint(const memory::MemoryConfig& config,
                          memory::MaintenanceReport& report) override {
    (void)config;
    if (!is_open_) {
      append_warning(report, "maintenance_store_not_open");
    }
  }

  [[nodiscard]] int run_turn_retention(const memory::MemoryConfig& config,
                                       memory::MaintenanceReport& report) override {
    (void)config;
    if (!is_open_) {
      append_warning(report, "maintenance_store_not_open");
    }
    return 0;
  }

  [[nodiscard]] int run_fact_retention(const memory::MemoryConfig& config,
                                       memory::MaintenanceReport& report) override {
    (void)config;
    if (!is_open_) {
      append_warning(report, "maintenance_store_not_open");
    }
    return 0;
  }

  [[nodiscard]] int run_experience_retention(
      const memory::MemoryConfig& config,
      memory::MaintenanceReport& report) override {
    (void)config;
    if (!is_open_) {
      append_warning(report, "maintenance_store_not_open");
    }
    return 0;
  }

  [[nodiscard]] int run_quarantine_cleanup(const memory::MemoryConfig& config,
                                           memory::MaintenanceReport& report) override {
    (void)config;
    if (!is_open_) {
      append_warning(report, "maintenance_store_not_open");
    }
    return 0;
  }

  [[nodiscard]] bool is_open_for_test() const { return is_open_; }
  [[nodiscard]] bool has_active_transaction_for_test() const { return active_transaction_; }
  [[nodiscard]] memory::StorageBackend last_open_backend_for_test() const { return last_open_backend_; }

 private:
  class FakeStoreTransaction final : public memory::IStoreTransaction {
   public:
    explicit FakeStoreTransaction(FakeMemoryStore& owner) : owner_(owner) {}

    ~FakeStoreTransaction() override {
      if (active_) {
        rollback();
      }
    }

    [[nodiscard]] std::optional<contracts::ResultCode> commit() override {
      active_ = false;
      owner_.active_transaction_ = false;
      return std::nullopt;
    }

    void rollback() noexcept override {
      active_ = false;
      owner_.active_transaction_ = false;
    }

   private:
    FakeMemoryStore& owner_;
    bool active_ = true;
  };

  struct QuarantineRecord {
    std::string object_type;
    std::string object_id;
    std::string reason;
  };

  [[nodiscard]] static std::optional<std::string> required_id(
      const std::optional<std::string>& value) {
    if (!value.has_value() || value->empty()) {
      return std::nullopt;
    }

    return value;
  }

  [[nodiscard]] bool session_matches_user(const std::optional<std::string>& session_id,
                                          const std::string& user_id) const {
    if (!session_id.has_value()) {
      return false;
    }

    const auto session_it = sessions_.find(*session_id);
    if (session_it == sessions_.end() || !session_it->second.user_id.has_value()) {
      return false;
    }

    return session_it->second.user_id == user_id;
  }

  [[nodiscard]] static bool matches_stage(const contracts::ExperienceMemory& experience,
                                          const std::string& stage) {
    if (!experience.tags.has_value()) {
      return false;
    }

    return std::any_of(experience.tags->begin(), experience.tags->end(),
                       [&stage](const std::string& tag) {
                         return tag == stage || tag == std::string{"stage:"} + stage;
                       });
  }

  [[nodiscard]] static bool matches_any_domain(
      const std::optional<std::vector<std::string>>& experience_domains,
      const std::vector<std::string>& query_domains) {
    if (!experience_domains.has_value()) {
      return false;
    }

    return std::any_of(query_domains.begin(), query_domains.end(),
                       [&experience_domains](const std::string& query_domain) {
                         return std::find(experience_domains->begin(),
                                          experience_domains->end(),
                                          query_domain) != experience_domains->end();
                       });
  }

  static void append_warning(memory::MaintenanceReport& report,
                             std::string warning) {
    if (std::find(report.warnings.begin(), report.warnings.end(), warning) ==
        report.warnings.end()) {
      report.warnings.push_back(std::move(warning));
    }
  }

  bool is_open_ = false;
  bool active_transaction_ = false;
  memory::StorageBackend last_open_backend_{};
  std::unordered_map<std::string, contracts::Session> sessions_;
  std::unordered_map<std::string, std::vector<contracts::Turn>> turns_by_session_;
  std::unordered_map<std::string, contracts::SummaryMemory> summaries_by_session_;
  std::unordered_map<std::string, contracts::MemoryFact> facts_by_id_;
  std::unordered_map<std::string, contracts::ExperienceMemory> experiences_by_id_;
  std::vector<QuarantineRecord> quarantine_records_;
};

}  // namespace dasall::tests::mocks
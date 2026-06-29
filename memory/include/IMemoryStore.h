#pragma once

#include <optional>

#include "IExperienceStore.h"
#include "IFactStore.h"
#include "IMaintenanceStore.h"
#include "ISessionStore.h"
#include "ISummaryStore.h"
#include "IProgrammaticMemoryStore.h"
#include "ITransactionalStore.h"
#include "config/MemoryConfig.h"

namespace dasall::memory {

class IMemoryStore : public ITransactionalStore,
           public IProgrammaticMemoryStore {
 public:
  virtual ~IMemoryStore() = default;

  [[nodiscard]] virtual std::optional<contracts::ResultCode> open(
      const MemoryConfig& config) = 0;
  virtual void close() noexcept = 0;

  [[nodiscard]] virtual SessionLoadBundle load_session_bundle(
    const SessionLoadRequest& request) const = 0;
  [[nodiscard]] virtual StoreResult create_session(
    const contracts::Session& session) = 0;
  [[nodiscard]] virtual StoreResult append_turn(
    const contracts::Turn& turn) = 0;
  [[nodiscard]] virtual StoreResult update_session_active(
    const std::string& session_id, std::int64_t last_active_at) = 0;

  [[nodiscard]] virtual StoreResult upsert_summary(
    const contracts::SummaryMemory& summary) = 0;
  [[nodiscard]] virtual std::optional<contracts::SummaryMemory> load_latest_summary(
    const std::string& session_id) const = 0;
  [[nodiscard]] virtual std::optional<contracts::SummaryMemory> load_latest_summary(
    const std::string& session_id,
    HierarchicalSummaryLevel level) const {
  const auto summary = load_latest_summary(session_id);
  if (!summary.has_value() || !summary_matches_level(*summary, level)) {
    return std::nullopt;
  }
  return summary;
  }
  [[nodiscard]] virtual std::vector<contracts::SummaryMemory> load_unparented_summaries(
    const std::string& session_id,
    HierarchicalSummaryLevel level,
    std::size_t limit) const {
  (void)session_id;
  (void)level;
  (void)limit;
  return {};
  }
  [[nodiscard]] virtual StoreResult assign_summary_parent(
    const std::vector<std::string>& summary_ids,
    const std::string& parent_summary_id) {
  (void)summary_ids;
  (void)parent_summary_id;
  return StoreResult::success();
  }

  [[nodiscard]] virtual FactQueryResult query_facts(
    const FactQuery& query) const = 0;
  [[nodiscard]] virtual FactQueryResult query_facts_by_user(
    const std::string& user_id,
    const FactQuery& query) const = 0;
  [[nodiscard]] virtual StoreResult touch_facts(
    const std::vector<std::string>& fact_ids,
    std::int64_t accessed_at) = 0;
  [[nodiscard]] virtual StoreResult insert_fact(
    const contracts::MemoryFact& fact) = 0;
  [[nodiscard]] virtual StoreResult supersede_fact(
    const std::string& old_fact_id,
    const std::string& new_fact_id) = 0;

  [[nodiscard]] virtual ExperienceQueryResult query_experiences(
    const ExperienceQuery& query) const = 0;
  [[nodiscard]] virtual StoreResult touch_experiences(
    const std::vector<std::string>& experience_ids,
    std::int64_t accessed_at) = 0;
  [[nodiscard]] virtual StoreResult insert_experience(
    const contracts::ExperienceMemory& experience) = 0;

  [[nodiscard]] virtual std::int64_t count_turns(
    const std::string& session_id) const = 0;
  [[nodiscard]] virtual StoreResult quarantine_record(
    const std::string& object_type,
    const std::string& object_id,
    const std::string& reason) = 0;
  virtual void run_wal_checkpoint(
    const MemoryConfig& config,
    MaintenanceReport& report) = 0;
  [[nodiscard]] virtual int run_turn_retention(
    const MemoryConfig& config,
    MaintenanceReport& report) = 0;
  [[nodiscard]] virtual int run_fact_retention(
    const MemoryConfig& config,
    MaintenanceReport& report) = 0;
  [[nodiscard]] virtual int run_experience_retention(
    const MemoryConfig& config,
    MaintenanceReport& report) = 0;
  [[nodiscard]] virtual int run_quarantine_cleanup(
    const MemoryConfig& config,
    MaintenanceReport& report) = 0;
};

}  // namespace dasall::memory
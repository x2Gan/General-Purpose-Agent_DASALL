#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "IMemoryStore.h"
#include "store/sqlite/SqliteSchemaMigrator.h"

struct sqlite3;

namespace dasall::memory::store::sqlite {

class SqliteMemoryStore final : public IMemoryStore {
 public:
  SqliteMemoryStore();
  ~SqliteMemoryStore() override;

  [[nodiscard]] std::optional<contracts::ResultCode> open(
      const MemoryConfig& config) override;
  void close() override;

  [[nodiscard]] std::unique_ptr<IStoreTransaction> begin_immediate() override;

  [[nodiscard]] SessionLoadBundle load_session_bundle(
      const SessionLoadRequest& request) override;
  [[nodiscard]] StoreResult create_session(
      const contracts::Session& session) override;
  [[nodiscard]] StoreResult append_turn(
      const contracts::Turn& turn) override;
  [[nodiscard]] StoreResult update_session_active(
      const std::string& session_id,
      std::int64_t last_active_at) override;

  [[nodiscard]] StoreResult upsert_summary(
      const contracts::SummaryMemory& summary) override;
  [[nodiscard]] std::optional<contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id) override;

  [[nodiscard]] FactQueryResult query_facts(const FactQuery& query) override;
  [[nodiscard]] StoreResult insert_fact(
      const contracts::MemoryFact& fact) override;
  [[nodiscard]] StoreResult supersede_fact(
      const std::string& old_fact_id,
      const std::string& new_fact_id) override;

  [[nodiscard]] ExperienceQueryResult query_experiences(
      const ExperienceQuery& query) override;
  [[nodiscard]] StoreResult insert_experience(
      const contracts::ExperienceMemory& experience) override;

  [[nodiscard]] std::int64_t count_turns(
      const std::string& session_id) override;
  [[nodiscard]] StoreResult quarantine_record(
      const std::string& object_type,
      const std::string& object_id,
      const std::string& reason) override;

 private:
  [[nodiscard]] sqlite3* select_reader_connection();

  sqlite3* writer_connection_ = nullptr;
  std::vector<sqlite3*> reader_connections_;
  std::size_t next_reader_index_ = 0;
  std::optional<MemoryConfig> config_;
  std::unique_ptr<SqliteSchemaMigrator> migrator_;
};

[[nodiscard]] std::unique_ptr<IMemoryStore> create_sqlite_memory_store();

}  // namespace dasall::memory::store::sqlite

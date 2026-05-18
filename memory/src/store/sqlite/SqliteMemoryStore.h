#pragma once

#include <atomic>
#include <memory>
#include <mutex>
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
  void close() noexcept override;

  [[nodiscard]] std::unique_ptr<IStoreTransaction> begin_immediate() override;

  [[nodiscard]] SessionLoadBundle load_session_bundle(
      const SessionLoadRequest& request) const override;
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
      const std::string& session_id) const override;

  [[nodiscard]] FactQueryResult query_facts(const FactQuery& query) const override;
  [[nodiscard]] StoreResult insert_fact(
      const contracts::MemoryFact& fact) override;
  [[nodiscard]] StoreResult supersede_fact(
      const std::string& old_fact_id,
      const std::string& new_fact_id) override;

  [[nodiscard]] ExperienceQueryResult query_experiences(
      const ExperienceQuery& query) const override;
  [[nodiscard]] StoreResult insert_experience(
      const contracts::ExperienceMemory& experience) override;

  [[nodiscard]] std::int64_t count_turns(
      const std::string& session_id) const override;
  [[nodiscard]] StoreResult quarantine_record(
      const std::string& object_type,
      const std::string& object_id,
      const std::string& reason) override;
  void run_wal_checkpoint(
      const MemoryConfig& config,
      MaintenanceReport& report) override;
  [[nodiscard]] int run_turn_retention(
      const MemoryConfig& config,
      MaintenanceReport& report) override;
  [[nodiscard]] int run_fact_retention(
      const MemoryConfig& config,
      MaintenanceReport& report) override;
  [[nodiscard]] int run_experience_retention(
      const MemoryConfig& config,
      MaintenanceReport& report) override;
  [[nodiscard]] int run_quarantine_cleanup(
      const MemoryConfig& config,
      MaintenanceReport& report) override;

  [[nodiscard]] sqlite3* writer_connection_for_maintenance();

 private:
    struct ReaderConnectionLease {
        sqlite3* connection = nullptr;
        std::unique_lock<std::mutex> guard;
    };

    [[nodiscard]] ReaderConnectionLease select_reader_connection() const;

  sqlite3* writer_connection_ = nullptr;
  std::vector<sqlite3*> reader_connections_;
    std::vector<std::unique_ptr<std::mutex>> reader_connection_guards_;
    mutable std::atomic<std::size_t> next_reader_index_{0};
  std::optional<MemoryConfig> config_;
  std::unique_ptr<SqliteSchemaMigrator> migrator_;
};

[[nodiscard]] std::unique_ptr<IMemoryStore> create_sqlite_memory_store();

}  // namespace dasall::memory::store::sqlite

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "error/ResultCode.h"

struct sqlite3;

namespace dasall::memory::store::sqlite {

struct MigrationFile {
  int version = 0;
  std::string description;
  std::string sql_content;
  std::string checksum;
};

struct MigrationStatus {
  int current_version = 0;
  int target_version = 0;
  bool up_to_date = false;
};

class SqliteSchemaMigrator {
 public:
  explicit SqliteSchemaMigrator(std::string migrations_dir);

  [[nodiscard]] std::optional<contracts::ResultCode> migrate(sqlite3* writer_conn) const;
  [[nodiscard]] MigrationStatus status(sqlite3* reader_conn) const;

 private:
  [[nodiscard]] std::vector<MigrationFile> load_migration_files() const;
  [[nodiscard]] std::optional<contracts::ResultCode> apply_migration(
      sqlite3* conn,
      const MigrationFile& file) const;
  [[nodiscard]] bool verify_checksum(sqlite3* conn,
                                     int version,
                                     const std::string& expected_checksum) const;
  [[nodiscard]] bool ensure_migrations_table(sqlite3* conn) const;

  std::string migrations_dir_;
};

}  // namespace dasall::memory::store::sqlite

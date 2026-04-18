#include "store/sqlite/SqliteSchemaMigrator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "error/MemoryError.h"

namespace dasall::memory::store::sqlite {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256Table = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value,
                                                    std::uint32_t amount) {
  return (value >> amount) | (value << (32U - amount));
}

[[nodiscard]] std::string sha256_hex(const std::string& input) {
  std::array<std::uint32_t, 8> hash = {
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  };

  std::vector<std::uint8_t> message(input.begin(), input.end());
  const std::uint64_t bit_length = static_cast<std::uint64_t>(message.size()) * 8U;
  message.push_back(0x80U);
  while ((message.size() % 64U) != 56U) {
    message.push_back(0x00U);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
  }

  for (std::size_t chunk_offset = 0; chunk_offset < message.size(); chunk_offset += 64U) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0; index < 16U; ++index) {
      const std::size_t byte_offset = chunk_offset + index * 4U;
      schedule[index] = (static_cast<std::uint32_t>(message[byte_offset]) << 24U) |
                        (static_cast<std::uint32_t>(message[byte_offset + 1U]) << 16U) |
                        (static_cast<std::uint32_t>(message[byte_offset + 2U]) << 8U) |
                        static_cast<std::uint32_t>(message[byte_offset + 3U]);
    }

    for (std::size_t index = 16U; index < 64U; ++index) {
      const std::uint32_t small_sigma0 =
          rotate_right(schedule[index - 15U], 7U) ^
          rotate_right(schedule[index - 15U], 18U) ^
          (schedule[index - 15U] >> 3U);
      const std::uint32_t small_sigma1 =
          rotate_right(schedule[index - 2U], 17U) ^
          rotate_right(schedule[index - 2U], 19U) ^
          (schedule[index - 2U] >> 10U);
      schedule[index] = schedule[index - 16U] + small_sigma0 +
                        schedule[index - 7U] + small_sigma1;
    }

    std::uint32_t a = hash[0];
    std::uint32_t b = hash[1];
    std::uint32_t c = hash[2];
    std::uint32_t d = hash[3];
    std::uint32_t e = hash[4];
    std::uint32_t f = hash[5];
    std::uint32_t g = hash[6];
    std::uint32_t h = hash[7];

    for (std::size_t index = 0; index < 64U; ++index) {
      const std::uint32_t big_sigma1 =
          rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
      const std::uint32_t choice = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 = h + big_sigma1 + choice +
                                  kSha256Table[index] + schedule[index];
      const std::uint32_t big_sigma0 =
          rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = big_sigma0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto value : hash) {
    stream << std::setw(8) << value;
  }
  return stream.str();
}

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::optional<contracts::ResultCode> map_sqlite_status(int sqlite_status) {
  switch (sqlite_status) {
    case SQLITE_OK:
    case SQLITE_DONE:
    case SQLITE_ROW:
      return std::nullopt;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
      return map_memory_error(MemoryError::StorageBusy).result_code;
    case SQLITE_CANTOPEN:
    case SQLITE_IOERR:
    case SQLITE_FULL:
    case SQLITE_READONLY:
      return map_memory_error(MemoryError::StorageUnavailable).result_code;
    case SQLITE_MISUSE:
      return map_memory_error(MemoryError::ConfigInvalid).result_code;
    default:
      return map_memory_error(MemoryError::SchemaMismatch).result_code;
  }
}

[[nodiscard]] std::optional<contracts::ResultCode> exec_sql(sqlite3* conn,
                                                             std::string_view sql) {
  char* error_message = nullptr;
  const int sqlite_status = sqlite3_exec(
      conn, std::string(sql).c_str(), nullptr, nullptr, &error_message);
  if (error_message != nullptr) {
    sqlite3_free(error_message);
  }
  return map_sqlite_status(sqlite_status);
}

[[nodiscard]] std::map<int, std::string> load_applied_migrations(sqlite3* conn) {
  std::map<int, std::string> applied_versions;
  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT version, checksum FROM schema_migrations ORDER BY version ASC";
  if (sqlite3_prepare_v2(conn, query, -1, &statement, nullptr) != SQLITE_OK) {
    return applied_versions;
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    const int version = sqlite3_column_int(statement, 0);
    const auto* checksum = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    applied_versions.emplace(version, checksum == nullptr ? std::string{} : checksum);
  }

  sqlite3_finalize(statement);
  return applied_versions;
}

[[nodiscard]] int current_version(sqlite3* conn) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto query = "SELECT COALESCE(MAX(version), 0) FROM schema_migrations";
  if (sqlite3_prepare_v2(conn, query, -1, &statement, nullptr) != SQLITE_OK) {
    return 0;
  }

  int version = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    version = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return version;
}

}  // namespace

SqliteSchemaMigrator::SqliteSchemaMigrator(std::string migrations_dir)
    : migrations_dir_(std::move(migrations_dir)) {}

std::optional<contracts::ResultCode> SqliteSchemaMigrator::migrate(
    sqlite3* writer_conn) const {
  if (writer_conn == nullptr) {
    return map_memory_error(MemoryError::ConfigInvalid).result_code;
  }

  if (!ensure_migrations_table(writer_conn)) {
    return map_memory_error(MemoryError::SchemaMismatch).result_code;
  }

  const auto migration_files = load_migration_files();
  const auto applied_versions = load_applied_migrations(writer_conn);
  if (migration_files.empty()) {
    return applied_versions.empty()
               ? std::nullopt
               : std::optional<contracts::ResultCode>{
                     map_memory_error(MemoryError::ConfigInvalid).result_code};
  }

  std::map<int, MigrationFile> files_by_version;
  for (const auto& file : migration_files) {
    files_by_version.emplace(file.version, file);
  }

  for (const auto& [applied_version, applied_checksum] : applied_versions) {
    const auto file_it = files_by_version.find(applied_version);
    if (file_it == files_by_version.end() ||
        applied_checksum != file_it->second.checksum) {
      return map_memory_error(MemoryError::SchemaMismatch).result_code;
    }
  }

  for (const auto& file : migration_files) {
    if (applied_versions.contains(file.version)) {
      continue;
    }

    if (const auto result = apply_migration(writer_conn, file); result.has_value()) {
      return result;
    }
  }

  return std::nullopt;
}

MigrationStatus SqliteSchemaMigrator::status(sqlite3* reader_conn) const {
  MigrationStatus migration_status;
  if (reader_conn == nullptr) {
    return migration_status;
  }

  if (!ensure_migrations_table(reader_conn)) {
    return migration_status;
  }

  const auto migration_files = load_migration_files();
  migration_status.current_version = current_version(reader_conn);
  migration_status.target_version = migration_files.empty()
                                        ? migration_status.current_version
                                        : migration_files.back().version;
  migration_status.up_to_date =
      migration_status.current_version == migration_status.target_version;
  return migration_status;
}

std::vector<MigrationFile> SqliteSchemaMigrator::load_migration_files() const {
  namespace fs = std::filesystem;

  std::vector<MigrationFile> migration_files;
  const fs::path migrations_dir{migrations_dir_};
  if (!fs::exists(migrations_dir) || !fs::is_directory(migrations_dir)) {
    return migration_files;
  }

  const std::regex file_pattern(R"(^V([0-9]+)__(.+)\.sql$)");
  for (const auto& entry : fs::directory_iterator(migrations_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    std::smatch match;
    const auto filename = entry.path().filename().string();
    if (!std::regex_match(filename, match, file_pattern)) {
      continue;
    }

    std::ifstream stream(entry.path(), std::ios::binary);
    if (!stream.is_open()) {
      throw std::runtime_error("failed to open migration file");
    }

    const std::string sql_content{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>(),
    };

    migration_files.push_back(MigrationFile{
        .version = std::stoi(match[1].str()),
        .description = match[2].str(),
        .sql_content = sql_content,
        .checksum = sha256_hex(sql_content),
    });
  }

  std::sort(migration_files.begin(), migration_files.end(),
            [](const MigrationFile& left, const MigrationFile& right) {
              return left.version < right.version;
            });

  for (std::size_t index = 1; index < migration_files.size(); ++index) {
    if (migration_files[index - 1].version == migration_files[index].version) {
      throw std::runtime_error("duplicate migration version");
    }
  }

  return migration_files;
}

std::optional<contracts::ResultCode> SqliteSchemaMigrator::apply_migration(
    sqlite3* conn,
    const MigrationFile& file) const {
  if (const auto begin_result = exec_sql(conn, "BEGIN IMMEDIATE;");
      begin_result.has_value()) {
    return begin_result;
  }

  if (const auto execution_result = exec_sql(conn, file.sql_content);
      execution_result.has_value()) {
    (void)exec_sql(conn, "ROLLBACK;");
    return execution_result;
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto insert_sql =
      "INSERT INTO schema_migrations(version, description, checksum, applied_at) "
      "VALUES(?1, ?2, ?3, ?4)";
  if (sqlite3_prepare_v2(conn, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
    (void)exec_sql(conn, "ROLLBACK;");
    return map_memory_error(MemoryError::SchemaMismatch).result_code;
  }

  sqlite3_bind_int(statement, 1, file.version);
  sqlite3_bind_text(statement, 2, file.description.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, file.checksum.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 4, current_time_ms());

  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto bind_result = map_sqlite_status(step_status); bind_result.has_value()) {
    (void)exec_sql(conn, "ROLLBACK;");
    return bind_result;
  }

  if (const auto commit_result = exec_sql(conn, "COMMIT;");
      commit_result.has_value()) {
    (void)exec_sql(conn, "ROLLBACK;");
    return commit_result;
  }

  return std::nullopt;
}

bool SqliteSchemaMigrator::verify_checksum(sqlite3* conn,
                                           int version,
                                           const std::string& expected_checksum) const {
  sqlite3_stmt* statement = nullptr;
  constexpr auto query_sql =
      "SELECT checksum FROM schema_migrations WHERE version = ?1 LIMIT 1";
  if (sqlite3_prepare_v2(conn, query_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int(statement, 1, version);
  const int step_status = sqlite3_step(statement);
  if (step_status != SQLITE_ROW) {
    sqlite3_finalize(statement);
    return false;
  }

  const auto* checksum = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
  const bool matches = checksum != nullptr && expected_checksum == checksum;
  sqlite3_finalize(statement);
  return matches;
}

bool SqliteSchemaMigrator::ensure_migrations_table(sqlite3* conn) const {
  constexpr auto schema_sql =
      "CREATE TABLE IF NOT EXISTS schema_migrations("
      "version INTEGER PRIMARY KEY,"
      "description TEXT NOT NULL,"
      "checksum TEXT NOT NULL,"
      "applied_at INTEGER NOT NULL"
      ");";
  return !exec_sql(conn, schema_sql).has_value();
}

}  // namespace dasall::memory::store::sqlite

#include "index/IndexWriter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "error/ErrorInfoGuards.h"

namespace dasall::knowledge::index {

namespace {

using dasall::contracts::validate_error_info_required_fields;
using retrieve::SparseIndexSearchRequest;
using retrieve::SparseIndexSearchResult;
using retrieve::SparseSearchRow;

constexpr std::string_view kDefaultTokenizerProfile =
    "porter unicode61 remove_diacritics 1";
constexpr std::string_view kSnapshotIdPrefix = "snapshot:";
constexpr std::string_view kBatchIdPrefix = "batch:";

[[nodiscard]] bool has_unique_non_empty_values(const std::vector<std::string>& values) {
  std::set<std::string, std::less<>> unique_values;
  for (const auto& value : values) {
    if (value.empty() || !unique_values.insert(value).second) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] std::int64_t default_now_ms() {
  const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now());
  return now.time_since_epoch().count();
}

[[nodiscard]] std::filesystem::path default_snapshots_root() {
  return std::filesystem::temp_directory_path() / "dasall-knowledge-snapshots";
}

[[nodiscard]] bool matches_authority_level(AuthorityLevel candidate,
                                           AuthorityLevel required_minimum) {
  return static_cast<int>(candidate) <= static_cast<int>(required_minimum);
}

[[nodiscard]] std::string lower_ascii_copy(std::string_view value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const unsigned char character : value) {
    lowered.push_back(static_cast<char>(std::tolower(character)));
  }
  return lowered;
}

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool contains_all_tags(const std::vector<std::string>& candidate_tags,
                                     const std::vector<std::string>& required_tags) {
  if (required_tags.empty()) {
    return true;
  }

  std::vector<std::string> normalized_tags;
  normalized_tags.reserve(candidate_tags.size());
  for (const auto& tag : candidate_tags) {
    normalized_tags.push_back(lower_ascii_copy(tag));
  }

  return std::all_of(required_tags.begin(), required_tags.end(), [&](const auto& required_tag) {
    return contains_string(normalized_tags, lower_ascii_copy(required_tag));
  });
}

[[nodiscard]] bool matches_language(const std::optional<std::string>& candidate_language,
                                    const std::optional<std::string>& required_language) {
  if (!required_language.has_value()) {
    return true;
  }

  return candidate_language.has_value() &&
         lower_ascii_copy(*candidate_language) == lower_ascii_copy(*required_language);
}

[[nodiscard]] std::string serialize_tags(const std::vector<std::string>& tags) {
  std::ostringstream buffer;
  for (std::size_t index = 0U; index < tags.size(); ++index) {
    if (index > 0U) {
      buffer << '\n';
    }
    buffer << tags[index];
  }
  return buffer.str();
}

[[nodiscard]] std::vector<std::string> deserialize_tags(const std::string& serialized_tags) {
  std::vector<std::string> tags;
  std::istringstream buffer(serialized_tags);
  std::string line;
  while (std::getline(buffer, line)) {
    if (!line.empty()) {
      tags.push_back(std::move(line));
    }
  }
  return tags;
}

[[nodiscard]] std::string document_lineage_id_for(const ingest::ChunkRecord& record) {
  const auto iterator = record.metadata.find("document_lineage_id");
  if (iterator != record.metadata.end() && !iterator->second.empty()) {
    return iterator->second;
  }

  return record.document_id;
}

[[nodiscard]] std::string escape_sql_literal(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8U);
  for (const char character : value) {
    if (character == '\'') {
      escaped += "''";
      continue;
    }
    escaped.push_back(character);
  }
  return escaped;
}

struct Sha256Context {
  std::array<std::uint32_t, 8U> state{0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
                                      0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
  std::array<std::uint8_t, 64U> buffer{};
  std::uint64_t bit_count = 0U;
  std::size_t buffer_size = 0U;
};

constexpr std::array<std::uint32_t, 64U> kSha256RoundConstants = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U,
    0x923F82A4U, 0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U,
    0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U,
    0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
    0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU,
    0x5B9CCA4FU, 0x682E6FF3U, 0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U};

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t value, std::uint32_t shift) {
  return (value >> shift) | (value << (32U - shift));
}

void sha256_transform(Sha256Context& context, const std::uint8_t* block) {
  std::array<std::uint32_t, 64U> words{};
  for (std::size_t index = 0U; index < 16U; ++index) {
    const std::size_t offset = index * 4U;
    words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                   (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                   static_cast<std::uint32_t>(block[offset + 3U]);
  }

  for (std::size_t index = 16U; index < 64U; ++index) {
    const auto sigma0 = rotr(words[index - 15U], 7U) ^ rotr(words[index - 15U], 18U) ^
                        (words[index - 15U] >> 3U);
    const auto sigma1 = rotr(words[index - 2U], 17U) ^ rotr(words[index - 2U], 19U) ^
                        (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
  }

  auto a = context.state[0];
  auto b = context.state[1];
  auto c = context.state[2];
  auto d = context.state[3];
  auto e = context.state[4];
  auto f = context.state[5];
  auto g = context.state[6];
  auto h = context.state[7];

  for (std::size_t index = 0U; index < 64U; ++index) {
    const auto sigma1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
    const auto choice = (e & f) ^ ((~e) & g);
    const auto temp1 = h + sigma1 + choice + kSha256RoundConstants[index] + words[index];
    const auto sigma0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const auto temp2 = sigma0 + majority;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  context.state[0] += a;
  context.state[1] += b;
  context.state[2] += c;
  context.state[3] += d;
  context.state[4] += e;
  context.state[5] += f;
  context.state[6] += g;
  context.state[7] += h;
}

void sha256_update(Sha256Context& context, const std::uint8_t* data, std::size_t length) {
  for (std::size_t index = 0U; index < length; ++index) {
    context.buffer[context.buffer_size++] = data[index];
    if (context.buffer_size == context.buffer.size()) {
      sha256_transform(context, context.buffer.data());
      context.bit_count += 512U;
      context.buffer_size = 0U;
    }
  }
}

[[nodiscard]] std::string sha256_finalize(Sha256Context& context) {
  context.bit_count += static_cast<std::uint64_t>(context.buffer_size) * 8U;
  context.buffer[context.buffer_size++] = 0x80U;

  if (context.buffer_size > 56U) {
    while (context.buffer_size < 64U) {
      context.buffer[context.buffer_size++] = 0U;
    }
    sha256_transform(context, context.buffer.data());
    context.buffer_size = 0U;
  }

  while (context.buffer_size < 56U) {
    context.buffer[context.buffer_size++] = 0U;
  }

  for (std::size_t index = 0U; index < 8U; ++index) {
    context.buffer[63U - index] =
        static_cast<std::uint8_t>((context.bit_count >> (index * 8U)) & 0xFFU);
  }

  sha256_transform(context, context.buffer.data());

  std::ostringstream digest;
  digest << std::hex << std::setfill('0');
  for (const auto word : context.state) {
    digest << std::setw(8) << word;
  }
  return digest.str();
}

[[nodiscard]] std::string sha256_digest(std::string_view input) {
  Sha256Context context;
  sha256_update(context, reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
  return sha256_finalize(context);
}

[[nodiscard]] std::string slurp_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("snapshot_file_open_failed");
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

[[nodiscard]] std::string make_snapshot_id(std::string_view batch_id, std::int64_t built_at) {
  return std::string(kSnapshotIdPrefix) +
         sha256_digest(std::string(batch_id) + ":" + std::to_string(built_at));
}

[[nodiscard]] std::string build_rebuild_batch_id(const RebuildPlan& plan) {
  std::ostringstream seed;
  seed << plan.rebuild_reason << '\n' << plan.tokenizer_profile << '\n'
       << (plan.vector_enabled ? "1" : "0");
  for (const auto& chunk : plan.chunk_records) {
    seed << '\n' << chunk.chunk_id << '\n' << chunk.document_id << '\n' << chunk.corpus_id;
  }
  for (const auto& warning : plan.warnings) {
    seed << '\n' << warning;
  }

  return std::string(kBatchIdPrefix) + sha256_digest(seed.str());
}

[[nodiscard]] contracts::ErrorInfo make_writer_error(KnowledgeErrorCode code,
                                                     std::string message,
                                                     std::string stage,
                                                     std::string ref_id) {
  return make_knowledge_error_info(code, std::move(message), std::move(stage), std::move(ref_id));
}

template <typename Report>
[[nodiscard]] Report make_failed_report(KnowledgeErrorCode code,
                                        std::string message,
                                        std::string stage,
                                        std::string ref_id,
                                        std::vector<std::string> warnings = {}) {
  Report report;
  report.ok = false;
  report.warnings = std::move(warnings);
  report.error = make_writer_error(code, std::move(message), std::move(stage), std::move(ref_id));
  return report;
}

struct SqliteDatabase {
  sqlite3* handle = nullptr;

  SqliteDatabase() = default;
  explicit SqliteDatabase(sqlite3* connection)
      : handle(connection) {}

  SqliteDatabase(const SqliteDatabase&) = delete;
  SqliteDatabase& operator=(const SqliteDatabase&) = delete;

  SqliteDatabase(SqliteDatabase&& other) noexcept
      : handle(std::exchange(other.handle, nullptr)) {}

  SqliteDatabase& operator=(SqliteDatabase&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    if (handle != nullptr) {
      sqlite3_close(handle);
    }
    handle = std::exchange(other.handle, nullptr);
    return *this;
  }

  ~SqliteDatabase() {
    if (handle != nullptr) {
      sqlite3_close(handle);
    }
  }
};

struct SqliteStatement {
  sqlite3_stmt* handle = nullptr;

  SqliteStatement() = default;
  explicit SqliteStatement(sqlite3_stmt* statement)
      : handle(statement) {}

  SqliteStatement(const SqliteStatement&) = delete;
  SqliteStatement& operator=(const SqliteStatement&) = delete;

  SqliteStatement(SqliteStatement&& other) noexcept
      : handle(std::exchange(other.handle, nullptr)) {}

  SqliteStatement& operator=(SqliteStatement&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    if (handle != nullptr) {
      sqlite3_finalize(handle);
    }
    handle = std::exchange(other.handle, nullptr);
    return *this;
  }

  ~SqliteStatement() {
    if (handle != nullptr) {
      sqlite3_finalize(handle);
    }
  }
};

[[nodiscard]] SqliteDatabase open_database(const std::filesystem::path& path, int flags) {
  sqlite3* raw_database = nullptr;
  if (sqlite3_open_v2(path.string().c_str(), &raw_database, flags, nullptr) != SQLITE_OK) {
    std::string message = "sqlite_open_failed";
    if (raw_database != nullptr) {
      message = sqlite3_errmsg(raw_database);
      sqlite3_close(raw_database);
    }
    throw std::runtime_error(message);
  }

  return SqliteDatabase(raw_database);
}

void execute_sql(sqlite3* database, std::string_view sql) {
  char* error_message = nullptr;
  const auto result = sqlite3_exec(database, std::string(sql).c_str(), nullptr, nullptr,
                                   &error_message);
  if (result == SQLITE_OK) {
    return;
  }

  std::string message = error_message != nullptr ? error_message : "sqlite_exec_failed";
  sqlite3_free(error_message);
  throw std::runtime_error(message);
}

[[nodiscard]] SqliteStatement prepare_statement(sqlite3* database, std::string_view sql) {
  sqlite3_stmt* raw_statement = nullptr;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &raw_statement, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(database));
  }

  return SqliteStatement(raw_statement);
}

void initialize_schema(sqlite3* database, std::string_view tokenizer_profile) {
  execute_sql(database,
              "CREATE TABLE IF NOT EXISTS chunks ("
              "row_id INTEGER PRIMARY KEY AUTOINCREMENT,"
              "chunk_id TEXT NOT NULL UNIQUE,"
              "document_id TEXT NOT NULL,"
              "corpus_id TEXT NOT NULL,"
              "source_id TEXT NOT NULL,"
              "source_uri TEXT NOT NULL,"
              "chunk_text TEXT NOT NULL,"
              "citation_ref TEXT NOT NULL,"
              "updated_at INTEGER NOT NULL,"
              "authority_level INTEGER NOT NULL,"
              "language TEXT,"
              "tags TEXT NOT NULL,"
              "version TEXT NOT NULL,"
              "token_estimate INTEGER NOT NULL,"
              "span_begin INTEGER NOT NULL,"
              "span_end INTEGER NOT NULL,"
              "document_lineage_id TEXT NOT NULL"
              ");");
  execute_sql(database,
              "CREATE INDEX IF NOT EXISTS idx_chunks_document_lineage ON chunks(document_lineage_id);");
  execute_sql(database,
              "CREATE INDEX IF NOT EXISTS idx_chunks_corpus_id ON chunks(corpus_id);");

  const auto create_fts =
      "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(chunk_text, tokenize='" +
      escape_sql_literal(tokenizer_profile) + "');";
  execute_sql(database, create_fts);
}

void remove_document_lineage(sqlite3* database, std::string_view document_lineage_id) {
  auto delete_fts = prepare_statement(
      database,
      "DELETE FROM chunks_fts WHERE rowid IN (SELECT row_id FROM chunks WHERE document_lineage_id = ?1);");
  sqlite3_bind_text(delete_fts.handle, 1, document_lineage_id.data(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(delete_fts.handle) != SQLITE_DONE) {
    throw std::runtime_error(sqlite3_errmsg(database));
  }

  auto delete_chunks =
      prepare_statement(database, "DELETE FROM chunks WHERE document_lineage_id = ?1;");
  sqlite3_bind_text(delete_chunks.handle, 1, document_lineage_id.data(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(delete_chunks.handle) != SQLITE_DONE) {
    throw std::runtime_error(sqlite3_errmsg(database));
  }
}

void insert_chunk_record(sqlite3* database, const ingest::ChunkRecord& record) {
  const auto lineage_id = document_lineage_id_for(record);
  auto insert_chunk = prepare_statement(
      database,
      "INSERT INTO chunks(chunk_id, document_id, corpus_id, source_id, source_uri, chunk_text, "
      "citation_ref, updated_at, authority_level, language, tags, version, token_estimate, "
      "span_begin, span_end, document_lineage_id) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, "
      "?10, ?11, ?12, ?13, ?14, ?15, ?16);");

  sqlite3_bind_text(insert_chunk.handle, 1, record.chunk_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_chunk.handle, 2, record.document_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_chunk.handle, 3, record.corpus_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_chunk.handle, 4, record.source_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_chunk.handle, 5, record.source_uri.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_chunk.handle, 6, record.chunk_text.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_chunk.handle, 7, record.citation_ref.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(insert_chunk.handle, 8, record.updated_at_ms);
  sqlite3_bind_int(insert_chunk.handle, 9, static_cast<int>(record.authority_level));
  sqlite3_bind_text(insert_chunk.handle, 10, record.language.c_str(), -1, SQLITE_TRANSIENT);
  const auto serialized_tags = serialize_tags(record.tags);
  sqlite3_bind_text(insert_chunk.handle, 11, serialized_tags.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_chunk.handle, 12, record.version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(insert_chunk.handle, 13, static_cast<sqlite3_int64>(record.token_estimate));
  sqlite3_bind_int64(insert_chunk.handle, 14, static_cast<sqlite3_int64>(record.span_begin));
  sqlite3_bind_int64(insert_chunk.handle, 15, static_cast<sqlite3_int64>(record.span_end));
  sqlite3_bind_text(insert_chunk.handle, 16, lineage_id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(insert_chunk.handle) != SQLITE_DONE) {
    throw std::runtime_error(sqlite3_errmsg(database));
  }

  const auto row_id = sqlite3_last_insert_rowid(database);
  auto insert_fts =
      prepare_statement(database, "INSERT INTO chunks_fts(rowid, chunk_text) VALUES (?1, ?2);");
  sqlite3_bind_int64(insert_fts.handle, 1, row_id);
  sqlite3_bind_text(insert_fts.handle, 2, record.chunk_text.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(insert_fts.handle) != SQLITE_DONE) {
    throw std::runtime_error(sqlite3_errmsg(database));
  }
}

[[nodiscard]] std::size_t query_count(sqlite3* database, std::string_view sql) {
  auto statement = prepare_statement(database, sql);
  if (sqlite3_step(statement.handle) != SQLITE_ROW) {
    throw std::runtime_error(sqlite3_errmsg(database));
  }
  return static_cast<std::size_t>(sqlite3_column_int64(statement.handle, 0));
}

[[nodiscard]] std::string column_text_or_empty(sqlite3_stmt* statement, int column) {
  const auto* text = sqlite3_column_text(statement, column);
  return text == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(text));
}

[[nodiscard]] std::vector<ingest::ChunkRecord> load_current_chunk_records(sqlite3* database) {
  auto statement = prepare_statement(
      database,
      "SELECT chunk_id, document_id, corpus_id, source_id, source_uri, chunk_text, "
      "citation_ref, updated_at, authority_level, language, tags, version, token_estimate, "
      "span_begin, span_end, document_lineage_id FROM chunks ORDER BY row_id;");

  std::vector<ingest::ChunkRecord> records;
  while (sqlite3_step(statement.handle) == SQLITE_ROW) {
    ingest::ChunkRecord record;
    record.chunk_id = column_text_or_empty(statement.handle, 0);
    record.document_id = column_text_or_empty(statement.handle, 1);
    record.corpus_id = column_text_or_empty(statement.handle, 2);
    record.source_id = column_text_or_empty(statement.handle, 3);
    record.source_uri = column_text_or_empty(statement.handle, 4);
    record.chunk_text = column_text_or_empty(statement.handle, 5);
    record.citation_ref = column_text_or_empty(statement.handle, 6);
    record.updated_at_ms = sqlite3_column_int64(statement.handle, 7);
    record.authority_level = static_cast<AuthorityLevel>(sqlite3_column_int(statement.handle, 8));
    record.language = column_text_or_empty(statement.handle, 9);
    if (record.language.empty()) {
      record.language = "und";
    }
    record.tags = deserialize_tags(column_text_or_empty(statement.handle, 10));
    record.version = column_text_or_empty(statement.handle, 11);
    record.token_estimate = static_cast<std::size_t>(sqlite3_column_int64(statement.handle, 12));
    record.span_begin = static_cast<std::size_t>(sqlite3_column_int64(statement.handle, 13));
    record.span_end = static_cast<std::size_t>(sqlite3_column_int64(statement.handle, 14));

    const auto lineage_id = column_text_or_empty(statement.handle, 15);
    record.metadata = {
        {"document_class", "indexed_chunk"},
        {"section_path", "indexed_snapshot"},
        {"document_lineage_id", lineage_id.empty() ? record.document_id : lineage_id},
    };

    if (!record.has_consistent_values()) {
      throw std::runtime_error("current_chunk_record_invalid");
    }
    records.push_back(std::move(record));
  }

  return records;
}

[[nodiscard]] std::optional<IndexManifest> read_manifest_sidecar(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::nullopt;
  }

  std::map<std::string, std::string, std::less<>> fields;
  for (std::string line; std::getline(input, line);) {
    if (line.empty()) {
      continue;
    }

    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      return std::nullopt;
    }

    fields.emplace(line.substr(0, separator), line.substr(separator + 1U));
  }

  const auto format_version_it = fields.find("format_version");
  const auto lexical_backend_it = fields.find("lexical_backend");
  const auto tokenizer_profile_it = fields.find("tokenizer_profile");
  const auto snapshot_id_it = fields.find("snapshot_id");
  const auto built_at_it = fields.find("built_at");
  const auto effective_at_it = fields.find("effective_at");
  const auto document_count_it = fields.find("document_count");
  const auto chunk_count_it = fields.find("chunk_count");
  const auto vector_enabled_it = fields.find("vector_enabled");
  if (format_version_it == fields.end() || lexical_backend_it == fields.end() ||
      tokenizer_profile_it == fields.end() || snapshot_id_it == fields.end() ||
      built_at_it == fields.end() || effective_at_it == fields.end() ||
      document_count_it == fields.end() || chunk_count_it == fields.end() ||
      vector_enabled_it == fields.end()) {
    return std::nullopt;
  }

  try {
    IndexManifest manifest;
    manifest.format_version = static_cast<std::uint32_t>(std::stoul(format_version_it->second));
    manifest.lexical_backend = lexical_backend_it->second;
    manifest.tokenizer_profile = tokenizer_profile_it->second;
    manifest.snapshot_id = snapshot_id_it->second;
    manifest.built_at = std::stoll(built_at_it->second);
    manifest.effective_at = std::stoll(effective_at_it->second);
    manifest.document_count = static_cast<std::size_t>(std::stoull(document_count_it->second));
    manifest.chunk_count = static_cast<std::size_t>(std::stoull(chunk_count_it->second));
    if (vector_enabled_it->second == "1") {
      manifest.vector_enabled = true;
    } else if (vector_enabled_it->second == "0") {
      manifest.vector_enabled = false;
    } else {
      return std::nullopt;
    }

    return manifest.has_consistent_values() ? std::optional<IndexManifest>(std::move(manifest))
                                            : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

void write_manifest_sidecar(const std::filesystem::path& path, const IndexManifest& manifest) {
  std::ofstream output(path, std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("manifest_sidecar_open_failed");
  }

  output << "format_version=" << manifest.format_version << '\n';
  output << "lexical_backend=" << manifest.lexical_backend << '\n';
  output << "tokenizer_profile=" << manifest.tokenizer_profile << '\n';
  output << "snapshot_id=" << manifest.snapshot_id << '\n';
  output << "built_at=" << manifest.built_at << '\n';
  output << "effective_at=" << manifest.effective_at << '\n';
  output << "document_count=" << manifest.document_count << '\n';
  output << "chunk_count=" << manifest.chunk_count << '\n';
  output << "vector_enabled=" << (manifest.vector_enabled ? 1 : 0) << '\n';
}

[[nodiscard]] std::string compute_snapshot_checksum(const std::filesystem::path& database_path,
                                                    const std::filesystem::path& manifest_path) {
  return sha256_digest(slurp_file(database_path) + "\n--manifest--\n" + slurp_file(manifest_path));
}

[[nodiscard]] SparseIndexSearchResult make_search_error(KnowledgeErrorCode code,
                                                        std::string message,
                                                        std::string stage,
                                                        std::string ref_id) {
  SparseIndexSearchResult result;
  result.ok = false;
  result.error = make_knowledge_error_info(code, std::move(message), std::move(stage),
                                           std::move(ref_id));
  return result;
}

[[nodiscard]] SparseIndexSearchResult perform_search(const std::filesystem::path& database_path,
                                                     const SparseIndexSearchRequest& request) {
  if (!request.has_consistent_values()) {
    return make_search_error(KnowledgeErrorCode::QueryValidationFailed,
                             "sparse index search request is inconsistent",
                             "index_writer.snapshot_search", "search_request_invalid");
  }

  try {
    auto database = open_database(database_path, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX);
    auto statement = prepare_statement(
        database.handle,
        "SELECT c.corpus_id, c.document_id, c.chunk_id, bm25(chunks_fts) AS rank, c.chunk_text, "
        "c.citation_ref, c.updated_at, c.authority_level, c.language, c.tags "
        "FROM chunks_fts JOIN chunks c ON c.row_id = chunks_fts.rowid "
        "WHERE chunks_fts MATCH ?1 ORDER BY rank ASC LIMIT ?2;");

    sqlite3_bind_text(statement.handle, 1, request.expression.match_expression.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(statement.handle, 2,
                     static_cast<int>(std::max<std::size_t>(request.top_k * 8U, 32U)));

    SparseIndexSearchResult result;
    result.ok = true;
    int step_status = SQLITE_ROW;
    while ((step_status = sqlite3_step(statement.handle)) == SQLITE_ROW) {
      SparseSearchRow row;
      row.corpus_id = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 0));
      row.document_id = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 1));
      row.chunk_id = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 2));
      const auto rank = sqlite3_column_double(statement.handle, 3);
      row.score = static_cast<float>(1.0 / (1.0 + std::abs(rank)));
      row.chunk_text = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 4));
      row.citation_ref = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 5));
      row.updated_at = sqlite3_column_int64(statement.handle, 6);
      row.authority_level =
          static_cast<AuthorityLevel>(sqlite3_column_int(statement.handle, 7));
      if (sqlite3_column_type(statement.handle, 8) != SQLITE_NULL) {
        row.language = std::string(
            reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 8)));
      }
      row.tags = deserialize_tags(
          reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 9)));

      if (!request.allowed_corpus_ids.empty() &&
          !contains_string(request.allowed_corpus_ids, row.corpus_id)) {
        continue;
      }
      if (!matches_authority_level(row.authority_level, request.minimum_authority_level) ||
          !matches_language(row.language, request.required_language) ||
          !contains_all_tags(row.tags, request.required_tags)) {
        continue;
      }
      if (!row.has_consistent_values()) {
        return make_search_error(KnowledgeErrorCode::InternalError,
                                 "snapshot search produced inconsistent row",
                                 "index_writer.snapshot_search", "search_row_inconsistent");
      }

      result.rows.push_back(std::move(row));
      if (result.rows.size() >= request.top_k) {
        break;
      }
    }

    if (step_status != SQLITE_DONE && step_status != SQLITE_ROW) {
      return make_search_error(KnowledgeErrorCode::IndexUnavailable,
                               sqlite3_errmsg(database.handle),
                               "index_writer.snapshot_search", "search_execution_failed");
    }

    if (!result.has_consistent_values()) {
      return make_search_error(KnowledgeErrorCode::InternalError,
                               "snapshot search result is inconsistent",
                               "index_writer.snapshot_search", "search_result_inconsistent");
    }

    return result;
  } catch (const std::exception& exception) {
    return make_search_error(KnowledgeErrorCode::IndexUnavailable,
                             std::string("snapshot search failed: ") + exception.what(),
                             "index_writer.snapshot_search", "search_failed");
  }
}

[[nodiscard]] std::shared_ptr<const IndexSnapshot> make_snapshot(
    const std::filesystem::path& database_path,
    const IndexManifest& manifest,
    std::string checksum) {
  auto snapshot = std::make_shared<IndexSnapshot>();
  snapshot->manifest = manifest;
  snapshot->checksum = std::move(checksum);
  snapshot->search = [database_path](const SparseIndexSearchRequest& request) {
    return perform_search(database_path, request);
  };
  return snapshot;
}

void cleanup_shadow_directory(const std::filesystem::path& snapshot_dir) {
  std::error_code error;
  std::filesystem::remove_all(snapshot_dir, error);
}

void append_warning(std::vector<std::string>& warnings, std::string warning) {
  if (warning.empty() || std::find(warnings.begin(), warnings.end(), warning) != warnings.end()) {
    return;
  }

  warnings.push_back(std::move(warning));
}

}  // namespace

bool UpdateReport::has_consistent_values() const {
  if (!has_unique_non_empty_values(warnings)) {
    return false;
  }

  if (ok) {
    return manifest.has_value() && manifest->has_consistent_values() &&
           manifest->snapshot_id == snapshot_id && !error.has_value();
  }

  return !ok && error.has_value() && validate_error_info_required_fields(*error).ok &&
         (!manifest.has_value() || manifest->has_consistent_values());
}

bool RebuildPlan::has_consistent_values() const {
  if (rebuild_reason.empty() || tokenizer_profile.empty() || !has_unique_non_empty_values(warnings)) {
    return false;
  }

  std::set<std::string, std::less<>> chunk_ids;
  for (const auto& chunk : chunk_records) {
    if (!chunk.has_consistent_values() || !chunk_ids.insert(chunk.chunk_id).second) {
      return false;
    }
  }

  return true;
}

bool RebuildReport::has_consistent_values() const {
  if (!has_unique_non_empty_values(warnings)) {
    return false;
  }

  if (ok) {
    return manifest.has_value() && manifest->has_consistent_values() &&
           manifest->snapshot_id == snapshot_id && !error.has_value();
  }

  return !ok && error.has_value() && validate_error_info_required_fields(*error).ok &&
         (!manifest.has_value() || manifest->has_consistent_values());
}

bool DenseSnapshotBuildRequest::has_consistent_values() const {
  if (snapshot_dir.empty()) {
    return false;
  }

  std::set<std::string, std::less<>> chunk_ids;
  for (const auto& chunk : chunk_records) {
    if (!chunk.has_consistent_values() || !chunk_ids.insert(chunk.chunk_id).second) {
      return false;
    }
  }

  return true;
}

bool DenseSnapshotBuildResult::has_consistent_values() const {
  return warnings.empty() || has_unique_non_empty_values(warnings);
}

bool IndexWriter::ShadowIndex::has_consistent_values() const {
  return !snapshot_dir.empty() && !database_path.empty() && !manifest_path.empty() &&
         manifest.has_consistent_values() && !checksum.empty() && snapshot != nullptr &&
         snapshot->has_consistent_values();
}

IndexWriter::IndexWriter(IndexReader& reader,
                         VersionLedger& ledger,
                         IndexWriterDeps deps)
    : reader_(reader),
      ledger_(ledger),
      deps_(std::move(deps)) {
  if (!deps_.snapshots_root) {
    deps_.snapshots_root = default_snapshots_root;
  }
  if (!deps_.now_ms) {
    deps_.now_ms = default_now_ms;
  }
  if (!deps_.record_candidate) {
    deps_.record_candidate = [this](const VersionLedgerEntry& entry) {
      return ledger_.record_candidate(entry);
    };
  }
  if (!deps_.mark_active) {
    deps_.mark_active = [this](std::string_view snapshot_id, std::int64_t activated_at) {
      return ledger_.mark_active(snapshot_id, activated_at);
    };
  }
  if (!deps_.refresh_catalog) {
    deps_.refresh_catalog = [](const IndexManifest&) { return true; };
  }
}

bool IndexWriter::restore_startup_state(std::string_view active_snapshot_id,
                                        std::string_view last_known_good_snapshot_id) {
  active_shadow_.reset();
  last_known_good_shadow_.reset();

  std::optional<ShadowIndex> recovered_last_known_good;
  if (!last_known_good_snapshot_id.empty()) {
    recovered_last_known_good = load_shadow_index(last_known_good_snapshot_id);
  }

  std::optional<ShadowIndex> recovered_active;
  if (!active_snapshot_id.empty()) {
    if (active_snapshot_id == last_known_good_snapshot_id &&
        recovered_last_known_good.has_value()) {
      recovered_active = recovered_last_known_good;
    } else {
      recovered_active = load_shadow_index(active_snapshot_id);
    }
  }

  if (recovered_last_known_good.has_value()) {
    last_known_good_shadow_ = recovered_last_known_good;
  }

  if (recovered_active.has_value()) {
    if (!reader_.swap_active_snapshot(recovered_active->snapshot)) {
      active_shadow_.reset();
      last_known_good_shadow_.reset();
      return false;
    }

    active_shadow_ = recovered_active;
    if (!last_known_good_shadow_.has_value()) {
      last_known_good_shadow_ = recovered_active;
    }
    return true;
  }

  if (recovered_last_known_good.has_value()) {
    if (!reader_.swap_active_snapshot(recovered_last_known_good->snapshot)) {
      last_known_good_shadow_.reset();
      return false;
    }

    active_shadow_ = recovered_last_known_good;
    last_known_good_shadow_ = recovered_last_known_good;
    return true;
  }

  (void)reader_.swap_active_snapshot(nullptr);
  return false;
}

std::optional<std::string> IndexWriter::read_persisted_snapshot_checksum(
    const std::filesystem::path& snapshots_root,
    std::string_view snapshot_id) {
  if (snapshot_id.empty()) {
    return std::nullopt;
  }

  const auto snapshot_dir = snapshots_root / std::string(snapshot_id);
  const auto database_path = snapshot_dir / "lexical.sqlite";
  const auto manifest_path = snapshot_dir / "manifest.txt";
  if (!std::filesystem::exists(database_path) || !std::filesystem::exists(manifest_path)) {
    return std::nullopt;
  }

  const auto manifest = read_manifest_sidecar(manifest_path);
  if (!manifest.has_value() || manifest->snapshot_id != snapshot_id) {
    return std::nullopt;
  }

  try {
    return compute_snapshot_checksum(database_path, manifest_path);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

UpdateReport IndexWriter::apply_update_batch(const ingest::IndexUpdateBatch& batch) {
  if (!batch.has_consistent_values()) {
    return make_failed_report<UpdateReport>(KnowledgeErrorCode::RefreshFailed,
                                            "index update batch is inconsistent",
                                            "index_writer.apply_update_batch",
                                            "batch_invalid");
  }

  std::vector<std::string> warnings = batch.warnings;
  auto tokenizer_profile = std::string(kDefaultTokenizerProfile);
  auto vector_enabled = batch.vector_enabled.value_or(false);
  if (const auto manifest = reader_.current_manifest(); manifest.has_value()) {
    tokenizer_profile = manifest->tokenizer_profile;
    if (!batch.vector_enabled.has_value()) {
      vector_enabled = manifest->vector_enabled;
    }
  }

  ShadowIndex shadow;
  try {
    shadow = build_shadow_index(batch,
                                warnings,
                                tokenizer_profile,
                                vector_enabled,
                                true);
  } catch (const std::exception& exception) {
    return make_failed_report<UpdateReport>(KnowledgeErrorCode::RefreshFailed,
                                            std::string("shadow build failed: ") +
                                                exception.what(),
                                            "index_writer.apply_update_batch",
                                            "shadow_build_failed", warnings);
  }

  const auto current_manifest = reader_.current_manifest();
  VersionLedgerEntry entry;
  entry.snapshot_id = shadow.manifest.snapshot_id;
  entry.parent_snapshot_id = current_manifest.has_value() ? current_manifest->snapshot_id : "";
  entry.batch_id = batch.batch_id;
  entry.built_at = shadow.manifest.built_at;
  entry.state = SnapshotState::Pending;
  entry.document_count = shadow.manifest.document_count;
  entry.chunk_count = shadow.manifest.chunk_count;
  entry.checksum = shadow.checksum;
  entry.rollback_eligible = false;

  if (!deps_.record_candidate(entry)) {
    cleanup_shadow_directory(shadow.snapshot_dir);
    return make_failed_report<UpdateReport>(KnowledgeErrorCode::RefreshFailed,
                                            "version ledger rejected candidate snapshot",
                                            "index_writer.apply_update_batch",
                                            "record_candidate_failed", warnings);
  }

  const auto previous_active_shadow = active_shadow_;
  if (!swap_active_snapshot(shadow)) {
    cleanup_shadow_directory(shadow.snapshot_dir);
    return make_failed_report<UpdateReport>(KnowledgeErrorCode::RefreshFailed,
                                            "active snapshot swap failed",
                                            "index_writer.apply_update_batch",
                                            "snapshot_swap_failed", warnings);
  }

  if (!deps_.mark_active(shadow.manifest.snapshot_id, shadow.manifest.effective_at)) {
    if (previous_active_shadow.has_value()) {
      (void)reader_.swap_active_snapshot(previous_active_shadow->snapshot);
    } else if (last_known_good_shadow_.has_value()) {
      (void)reader_.swap_active_snapshot(last_known_good_shadow_->snapshot);
    } else {
      (void)reader_.swap_active_snapshot(nullptr);
    }
    cleanup_shadow_directory(shadow.snapshot_dir);
    return make_failed_report<UpdateReport>(KnowledgeErrorCode::RefreshFailed,
                                            "version ledger activation failed",
                                            "index_writer.apply_update_batch",
                                            "mark_active_failed", warnings);
  }

  active_shadow_ = shadow;
  last_known_good_shadow_ = shadow;

  UpdateReport report;
  report.ok = true;
  report.snapshot_id = shadow.manifest.snapshot_id;
  report.manifest = shadow.manifest;
  report.warnings = warnings;
  try {
    if (!deps_.refresh_catalog(shadow.manifest)) {
      append_warning(report.warnings, "catalog_refresh_failed");
    }
  } catch (const std::exception&) {
    append_warning(report.warnings, "catalog_refresh_failed");
  }
  return report;
}

RebuildReport IndexWriter::rebuild_all(const RebuildPlan& plan) {
  if (!plan.has_consistent_values()) {
    return make_failed_report<RebuildReport>(KnowledgeErrorCode::RefreshFailed,
                                             "rebuild plan is inconsistent",
                                             "index_writer.rebuild_all",
                                             "rebuild_plan_invalid");
  }

  ingest::IndexUpdateBatch batch;
  batch.batch_id = build_rebuild_batch_id(plan);
  batch.chunk_records = plan.chunk_records;
  batch.warnings = plan.warnings;
  std::vector<std::string> warnings = plan.warnings;

  ShadowIndex shadow;
  try {
    shadow = build_shadow_index(batch,
                                warnings,
                                plan.tokenizer_profile,
                                plan.vector_enabled,
                                false);
  } catch (const std::exception& exception) {
    return make_failed_report<RebuildReport>(KnowledgeErrorCode::RefreshFailed,
                                             std::string("shadow rebuild failed: ") +
                                                 exception.what(),
                                             "index_writer.rebuild_all",
                                             "shadow_build_failed", warnings);
  }

  const auto current_manifest = reader_.current_manifest();
  VersionLedgerEntry entry;
  entry.snapshot_id = shadow.manifest.snapshot_id;
  entry.parent_snapshot_id = current_manifest.has_value() ? current_manifest->snapshot_id : "";
  entry.batch_id = batch.batch_id;
  entry.built_at = shadow.manifest.built_at;
  entry.state = SnapshotState::Pending;
  entry.document_count = shadow.manifest.document_count;
  entry.chunk_count = shadow.manifest.chunk_count;
  entry.checksum = shadow.checksum;
  entry.rollback_eligible = false;

  if (!deps_.record_candidate(entry)) {
    cleanup_shadow_directory(shadow.snapshot_dir);
    return make_failed_report<RebuildReport>(KnowledgeErrorCode::RefreshFailed,
                                             "version ledger rejected rebuild candidate",
                                             "index_writer.rebuild_all",
                                             "record_candidate_failed", warnings);
  }

  const auto previous_active_shadow = active_shadow_;
  if (!swap_active_snapshot(shadow)) {
    cleanup_shadow_directory(shadow.snapshot_dir);
    return make_failed_report<RebuildReport>(KnowledgeErrorCode::RefreshFailed,
                                             "active snapshot swap failed",
                                             "index_writer.rebuild_all",
                                             "snapshot_swap_failed", warnings);
  }

  if (!deps_.mark_active(shadow.manifest.snapshot_id, shadow.manifest.effective_at)) {
    if (previous_active_shadow.has_value()) {
      (void)reader_.swap_active_snapshot(previous_active_shadow->snapshot);
    } else if (last_known_good_shadow_.has_value()) {
      (void)reader_.swap_active_snapshot(last_known_good_shadow_->snapshot);
    } else {
      (void)reader_.swap_active_snapshot(nullptr);
    }
    cleanup_shadow_directory(shadow.snapshot_dir);
    return make_failed_report<RebuildReport>(KnowledgeErrorCode::RefreshFailed,
                                             "version ledger activation failed",
                                             "index_writer.rebuild_all",
                                             "mark_active_failed", warnings);
  }

  active_shadow_ = shadow;
  last_known_good_shadow_ = shadow;

  RebuildReport report;
  report.ok = true;
  report.snapshot_id = shadow.manifest.snapshot_id;
  report.manifest = shadow.manifest;
  report.warnings = warnings;
  try {
    if (!deps_.refresh_catalog(shadow.manifest)) {
      append_warning(report.warnings, "catalog_refresh_failed");
    }
  } catch (const std::exception&) {
    append_warning(report.warnings, "catalog_refresh_failed");
  }
  return report;
}

IndexWriter::ShadowIndex IndexWriter::build_shadow_index(
    const ingest::IndexUpdateBatch& batch) const {
  auto tokenizer_profile = std::string(kDefaultTokenizerProfile);
  auto vector_enabled = false;
  if (const auto manifest = reader_.current_manifest(); manifest.has_value()) {
    tokenizer_profile = manifest->tokenizer_profile;
    vector_enabled = manifest->vector_enabled;
  }

  std::vector<std::string> warnings = batch.warnings;
  return build_shadow_index(batch, warnings, tokenizer_profile, vector_enabled, true);
}

IndexWriter::ShadowIndex IndexWriter::build_shadow_index(
    const ingest::IndexUpdateBatch& batch,
    std::vector<std::string>& warnings,
    std::string_view tokenizer_profile,
    bool vector_enabled,
    bool seed_from_active) const {
  if (!batch.has_consistent_values() || tokenizer_profile.empty()) {
    throw std::invalid_argument("index_update_batch_invalid");
  }

  const auto built_at = deps_.now_ms();
  if (built_at <= 0) {
    throw std::runtime_error("snapshot_timestamp_invalid");
  }

  const auto snapshot_id = make_snapshot_id(batch.batch_id, built_at);
  const auto snapshot_dir = deps_.snapshots_root() / snapshot_id;
  std::filesystem::create_directories(snapshot_dir);

  const auto database_path = snapshot_dir / "lexical.sqlite";
  const auto manifest_path = snapshot_dir / "manifest.txt";

  if (seed_from_active && active_shadow_.has_value() &&
      std::filesystem::exists(active_shadow_->database_path)) {
    std::filesystem::copy_file(active_shadow_->database_path, database_path,
                               std::filesystem::copy_options::overwrite_existing);
  }

  auto database = open_database(database_path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                                SQLITE_OPEN_NOMUTEX);
  initialize_schema(database.handle, tokenizer_profile);

  execute_sql(database.handle, "BEGIN IMMEDIATE;");
  try {
    for (const auto& removed_document_id : batch.removed_document_ids) {
      remove_document_lineage(database.handle, removed_document_id);
    }
    for (const auto& chunk : batch.chunk_records) {
      insert_chunk_record(database.handle, chunk);
    }
    execute_sql(database.handle, "COMMIT;");
  } catch (...) {
    execute_sql(database.handle, "ROLLBACK;");
    throw;
  }

  bool dense_snapshot_ready = false;
  if (vector_enabled) {
    if (!deps_.build_dense_snapshot) {
      append_warning(warnings, "dense_snapshot_builder_missing");
    } else {
      auto dense_chunk_records = load_current_chunk_records(database.handle);
      const auto dense_result = deps_.build_dense_snapshot(DenseSnapshotBuildRequest{
          .snapshot_dir = snapshot_dir,
          .chunk_records = std::move(dense_chunk_records),
      });
      if (!dense_result.has_consistent_values()) {
        throw std::runtime_error("dense_snapshot_result_inconsistent");
      }

      dense_snapshot_ready = dense_result.ok;
      for (const auto& warning : dense_result.warnings) {
        append_warning(warnings, warning);
      }
      if (!dense_snapshot_ready) {
        append_warning(warnings, "dense_snapshot_unavailable");
      }
    }
  }

  IndexManifest manifest;
  manifest.tokenizer_profile = std::string(tokenizer_profile);
  manifest.snapshot_id = snapshot_id;
  manifest.built_at = built_at;
  manifest.effective_at = built_at;
  manifest.document_count =
      query_count(database.handle, "SELECT COUNT(DISTINCT document_lineage_id) FROM chunks;");
  manifest.chunk_count = query_count(database.handle, "SELECT COUNT(*) FROM chunks;");
  manifest.vector_enabled = vector_enabled && dense_snapshot_ready;

  write_manifest_sidecar(manifest_path, manifest);
  auto checksum = compute_snapshot_checksum(database_path, manifest_path);
  auto snapshot = make_snapshot(database_path, manifest, checksum);

  ShadowIndex shadow;
  shadow.snapshot_dir = snapshot_dir;
  shadow.database_path = database_path;
  shadow.manifest_path = manifest_path;
  shadow.manifest = manifest;
  shadow.checksum = std::move(checksum);
  shadow.snapshot = std::move(snapshot);

  if (!shadow.has_consistent_values()) {
    throw std::runtime_error("shadow_index_inconsistent");
  }

  return shadow;
}

std::optional<IndexWriter::ShadowIndex> IndexWriter::load_shadow_index(
    std::string_view snapshot_id) const {
  if (snapshot_id.empty()) {
    return std::nullopt;
  }

  const auto snapshot_dir = deps_.snapshots_root() / std::string(snapshot_id);
  const auto database_path = snapshot_dir / "lexical.sqlite";
  const auto manifest_path = snapshot_dir / "manifest.txt";
  if (!std::filesystem::exists(database_path) || !std::filesystem::exists(manifest_path)) {
    return std::nullopt;
  }

  const auto manifest = read_manifest_sidecar(manifest_path);
  if (!manifest.has_value() || manifest->snapshot_id != snapshot_id) {
    return std::nullopt;
  }

  try {
    auto shadow = ShadowIndex{};
    shadow.snapshot_dir = snapshot_dir;
    shadow.database_path = database_path;
    shadow.manifest_path = manifest_path;
    shadow.manifest = *manifest;
    shadow.checksum = compute_snapshot_checksum(database_path, manifest_path);
    shadow.snapshot = make_snapshot(database_path, shadow.manifest, shadow.checksum);
    return shadow.has_consistent_values() ? std::optional<ShadowIndex>(std::move(shadow))
                                          : std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool IndexWriter::swap_active_snapshot(const ShadowIndex& shadow) {
  if (!shadow.has_consistent_values()) {
    return false;
  }

  return reader_.swap_active_snapshot(shadow.snapshot);
}

}  // namespace dasall::knowledge::index
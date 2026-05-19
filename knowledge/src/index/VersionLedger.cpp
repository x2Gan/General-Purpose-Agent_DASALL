#include "index/VersionLedger.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>

namespace dasall::knowledge::index {

namespace {

constexpr int kLedgerFormatVersion = 1;

using ScalarFields = std::map<std::string, std::string, std::less<>>;

template <typename EntryContainer>
auto find_entry(EntryContainer& entries, std::string_view snapshot_id) {
  return std::find_if(entries.begin(), entries.end(), [snapshot_id](const VersionLedgerEntry& entry) {
    return entry.snapshot_id == snapshot_id;
  });
}

[[nodiscard]] std::string fnv1a_hex(std::string_view content) {
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;

  std::uint64_t hash = kOffsetBasis;
  for (const char character : content) {
    hash ^= static_cast<unsigned char>(character);
    hash *= kPrime;
  }

  std::ostringstream output;
  output << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}

[[nodiscard]] std::string escape_json_string(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8U);
  for (const char character : value) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(character);
        break;
    }
  }
  return escaped;
}

bool skip_whitespace(std::string_view text, std::size_t& offset) {
  while (offset < text.size() &&
         std::isspace(static_cast<unsigned char>(text[offset])) != 0) {
    ++offset;
  }
  return offset < text.size();
}

bool parse_quoted_string(std::string_view text,
                         std::size_t& offset,
                         std::string& value) {
  if (offset >= text.size() || text[offset] != '"') {
    return false;
  }

  ++offset;
  value.clear();
  while (offset < text.size()) {
    const char character = text[offset++];
    if (character == '"') {
      return true;
    }

    if (character != '\\') {
      value.push_back(character);
      continue;
    }

    if (offset >= text.size()) {
      return false;
    }

    switch (text[offset++]) {
      case '\\':
        value.push_back('\\');
        break;
      case '"':
        value.push_back('"');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        return false;
    }
  }

  return false;
}

[[nodiscard]] std::optional<ScalarFields> parse_scalar_json_object(std::string_view text) {
  std::size_t offset = 0U;
  skip_whitespace(text, offset);
  if (offset >= text.size() || text[offset] != '{') {
    return std::nullopt;
  }

  ++offset;
  ScalarFields fields;
  skip_whitespace(text, offset);
  if (offset < text.size() && text[offset] == '}') {
    ++offset;
    skip_whitespace(text, offset);
    return offset == text.size() ? std::optional<ScalarFields>(std::move(fields))
                                 : std::nullopt;
  }

  while (offset < text.size()) {
    skip_whitespace(text, offset);

    std::string key;
    if (!parse_quoted_string(text, offset, key)) {
      return std::nullopt;
    }

    skip_whitespace(text, offset);
    if (offset >= text.size() || text[offset] != ':') {
      return std::nullopt;
    }
    ++offset;
    skip_whitespace(text, offset);

    std::string value;
    if (offset < text.size() && text[offset] == '"') {
      if (!parse_quoted_string(text, offset, value)) {
        return std::nullopt;
      }
    } else {
      const auto value_begin = offset;
      while (offset < text.size() && text[offset] != ',' && text[offset] != '}') {
        ++offset;
      }
      const auto value_end = offset;
      while (offset > value_begin &&
             std::isspace(static_cast<unsigned char>(text[offset - 1U])) != 0) {
        --offset;
      }
      value = std::string(text.substr(value_begin, offset - value_begin));
      offset = value_end;
    }

    fields.emplace(std::move(key), std::move(value));
    skip_whitespace(text, offset);
    if (offset >= text.size()) {
      return std::nullopt;
    }

    if (text[offset] == '}') {
      ++offset;
      skip_whitespace(text, offset);
      return offset == text.size() ? std::optional<ScalarFields>(std::move(fields))
                                   : std::nullopt;
    }

    if (text[offset] != ',') {
      return std::nullopt;
    }
    ++offset;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> parse_int64_field(const ScalarFields& fields,
                                                            std::string_view key) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end() || iterator->second.empty()) {
    return std::nullopt;
  }

  try {
    return std::stoll(iterator->second);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::size_t> parse_size_field(const ScalarFields& fields,
                                                          std::string_view key) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end() || iterator->second.empty()) {
    return std::nullopt;
  }

  try {
    return static_cast<std::size_t>(std::stoull(iterator->second));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<bool> parse_bool_field(const ScalarFields& fields,
                                                   std::string_view key) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end()) {
    return std::nullopt;
  }

  if (iterator->second == "true") {
    return true;
  }
  if (iterator->second == "false") {
    return false;
  }
  return std::nullopt;
}

[[nodiscard]] std::string snapshot_state_name(const SnapshotState state) {
  switch (state) {
    case SnapshotState::Pending:
      return "pending";
    case SnapshotState::Active:
      return "active";
    case SnapshotState::Superseded:
      return "superseded";
  }

  return "pending";
}

[[nodiscard]] std::optional<SnapshotState> parse_snapshot_state(std::string_view raw_state) {
  if (raw_state == "pending") {
    return SnapshotState::Pending;
  }
  if (raw_state == "active") {
    return SnapshotState::Active;
  }
  if (raw_state == "superseded") {
    return SnapshotState::Superseded;
  }
  return std::nullopt;
}

[[nodiscard]] std::string serialize_header_line() {
  return std::string{"{\"ledger_format_version\":"} + std::to_string(kLedgerFormatVersion) +
         '}';
}

[[nodiscard]] std::string serialize_checksum_line(std::string_view checksum) {
  return std::string{"{\"ledger_checksum\":\""} + escape_json_string(checksum) + "\"}";
}

[[nodiscard]] std::string serialize_entry_line(const VersionLedgerEntry& entry) {
  std::ostringstream output;
  output << '{'
         << "\"snapshot_id\":\"" << escape_json_string(entry.snapshot_id) << "\"," 
         << "\"parent_snapshot_id\":\"" << escape_json_string(entry.parent_snapshot_id) << "\"," 
         << "\"batch_id\":\"" << escape_json_string(entry.batch_id) << "\"," 
         << "\"built_at\":" << entry.built_at << ','
         << "\"activated_at\":" << entry.activated_at << ','
         << "\"state\":\"" << snapshot_state_name(entry.state) << "\"," 
         << "\"document_count\":" << entry.document_count << ','
         << "\"chunk_count\":" << entry.chunk_count << ','
         << "\"checksum\":\"" << escape_json_string(entry.checksum) << "\"," 
         << "\"rollback_eligible\":" << (entry.rollback_eligible ? "true" : "false")
         << '}';
  return output.str();
}

[[nodiscard]] std::optional<VersionLedgerEntry> parse_entry_line(std::string_view line) {
  const auto fields = parse_scalar_json_object(line);
  if (!fields.has_value()) {
    return std::nullopt;
  }

  VersionLedgerEntry entry;
  const auto snapshot_iterator = fields->find("snapshot_id");
  const auto parent_iterator = fields->find("parent_snapshot_id");
  const auto batch_iterator = fields->find("batch_id");
  const auto checksum_iterator = fields->find("checksum");
  const auto state_iterator = fields->find("state");
  if (snapshot_iterator == fields->end() || batch_iterator == fields->end() ||
      checksum_iterator == fields->end() || state_iterator == fields->end()) {
    return std::nullopt;
  }

  entry.snapshot_id = snapshot_iterator->second;
  entry.parent_snapshot_id =
      parent_iterator == fields->end() ? std::string{} : parent_iterator->second;
  entry.batch_id = batch_iterator->second;
  entry.checksum = checksum_iterator->second;

  const auto built_at = parse_int64_field(*fields, "built_at");
  const auto activated_at = parse_int64_field(*fields, "activated_at");
  const auto document_count = parse_size_field(*fields, "document_count");
  const auto chunk_count = parse_size_field(*fields, "chunk_count");
  const auto rollback_eligible = parse_bool_field(*fields, "rollback_eligible");
  const auto state = parse_snapshot_state(state_iterator->second);
  if (!built_at.has_value() || !activated_at.has_value() || !document_count.has_value() ||
      !chunk_count.has_value() || !rollback_eligible.has_value() || !state.has_value()) {
    return std::nullopt;
  }

  entry.built_at = *built_at;
  entry.activated_at = *activated_at;
  entry.document_count = *document_count;
  entry.chunk_count = *chunk_count;
  entry.rollback_eligible = *rollback_eligible;
  entry.state = *state;
  return entry.has_consistent_values() ? std::optional<VersionLedgerEntry>(std::move(entry))
                                       : std::nullopt;
}

[[nodiscard]] std::vector<VersionLedgerEntry> load_entries_from_disk(
    const std::filesystem::path& ledger_path) {
  if (ledger_path.empty() || !std::filesystem::exists(ledger_path)) {
    return {};
  }

  std::ifstream input(ledger_path, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }

  std::vector<std::string> lines;
  for (std::string line; std::getline(input, line);) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      lines.push_back(std::move(line));
    }
  }

  if (lines.size() < 2U) {
    return {};
  }

  const auto header = parse_scalar_json_object(lines.front());
  const auto footer = parse_scalar_json_object(lines.back());
  if (!header.has_value() || !footer.has_value()) {
    return {};
  }

  const auto format_version = parse_int64_field(*header, "ledger_format_version");
  const auto checksum_iterator = footer->find("ledger_checksum");
  if (!format_version.has_value() || *format_version != kLedgerFormatVersion ||
      checksum_iterator == footer->end()) {
    return {};
  }

  std::string payload;
  for (std::size_t index = 0U; index + 1U < lines.size(); ++index) {
    payload += lines[index];
    payload.push_back('\n');
  }
  if (fnv1a_hex(payload) != checksum_iterator->second) {
    return {};
  }

  std::vector<VersionLedgerEntry> entries;
  entries.reserve(lines.size() - 2U);
  for (std::size_t index = 1U; index + 1U < lines.size(); ++index) {
    const auto entry = parse_entry_line(lines[index]);
    if (!entry.has_value()) {
      return {};
    }
    entries.push_back(*entry);
  }

  return entries;
}

[[nodiscard]] bool participates_in_last_known_good(const VersionLedgerEntry& entry) {
  if (entry.state == SnapshotState::Active) {
    return true;
  }

  return entry.state == SnapshotState::Superseded && entry.rollback_eligible;
}

[[nodiscard]] bool is_better_last_known_good_candidate(const VersionLedgerEntry& candidate,
                                                       const VersionLedgerEntry& incumbent) {
  if (candidate.activated_at != incumbent.activated_at) {
    return candidate.activated_at > incumbent.activated_at;
  }

  if (candidate.state != incumbent.state) {
    return candidate.state == SnapshotState::Active;
  }

  if (candidate.built_at != incumbent.built_at) {
    return candidate.built_at > incumbent.built_at;
  }

  return candidate.snapshot_id > incumbent.snapshot_id;
}

[[nodiscard]] bool is_better_active_candidate(const VersionLedgerEntry& candidate,
                                              const VersionLedgerEntry& incumbent) {
  if (candidate.activated_at != incumbent.activated_at) {
    return candidate.activated_at > incumbent.activated_at;
  }

  if (candidate.built_at != incumbent.built_at) {
    return candidate.built_at > incumbent.built_at;
  }

  return candidate.snapshot_id > incumbent.snapshot_id;
}

}  // namespace

bool VersionLedgerEntry::has_consistent_values() const {
  if (snapshot_id.empty() || batch_id.empty() || built_at <= 0 || checksum.empty()) {
    return false;
  }

  if (!parent_snapshot_id.empty() && parent_snapshot_id == snapshot_id) {
    return false;
  }

  if (document_count == 0U && chunk_count != 0U) {
    return false;
  }

  switch (state) {
    case SnapshotState::Pending:
      return activated_at == 0 && !rollback_eligible;
    case SnapshotState::Active:
      return activated_at >= built_at && rollback_eligible;
    case SnapshotState::Superseded:
      return activated_at >= built_at;
  }

  return false;
}

VersionLedger::VersionLedger() = default;

VersionLedger::VersionLedger(VersionLedgerDeps deps)
    : deps_(std::move(deps)) {
  entries_ = load_entries_from_disk(deps_.ledger_path);
}

bool VersionLedger::record_candidate(const VersionLedgerEntry& entry) {
  if (!entry.has_consistent_values() || entry.state != SnapshotState::Pending ||
      entry.activated_at != 0 || entry.rollback_eligible) {
    return false;
  }

  if (find_entry(entries_, entry.snapshot_id) != entries_.end()) {
    return false;
  }

  if (!entry.parent_snapshot_id.empty() &&
      find_entry(entries_, entry.parent_snapshot_id) == entries_.end()) {
    return false;
  }

  entries_.push_back(entry);
  if (persist_entries()) {
    return true;
  }

  entries_.pop_back();
  return false;
}

bool VersionLedger::mark_active(std::string_view snapshot_id, std::int64_t activated_at) {
  if (snapshot_id.empty() || activated_at <= 0) {
    return false;
  }

  auto target_entry = find_entry(entries_, snapshot_id);
  if (target_entry == entries_.end() || target_entry->state != SnapshotState::Pending ||
      activated_at < target_entry->built_at) {
    return false;
  }

  const auto previous_entries = entries_;

  for (auto& entry : entries_) {
    if (entry.state == SnapshotState::Active) {
      entry.state = SnapshotState::Superseded;
      entry.rollback_eligible = true;
    }
  }

  target_entry->state = SnapshotState::Active;
  target_entry->activated_at = activated_at;
  target_entry->rollback_eligible = true;
  if (!target_entry->has_consistent_values() || !persist_entries()) {
    entries_ = previous_entries;
    return false;
  }

  return true;
}

bool VersionLedger::mark_superseded(std::string_view snapshot_id) {
  if (snapshot_id.empty()) {
    return false;
  }

  auto target_entry = find_entry(entries_, snapshot_id);
  if (target_entry == entries_.end() || target_entry->state == SnapshotState::Pending) {
    return false;
  }

  if (target_entry->state == SnapshotState::Superseded) {
    return target_entry->has_consistent_values() && persist_entries();
  }

  const auto previous_entries = entries_;

  target_entry->state = SnapshotState::Superseded;
  target_entry->rollback_eligible = true;
  if (!target_entry->has_consistent_values() || !persist_entries()) {
    entries_ = previous_entries;
    return false;
  }

  return true;
}

std::optional<VersionLedgerEntry> VersionLedger::active() const {
  std::optional<VersionLedgerEntry> best_entry;

  for (const auto& entry : entries_) {
    if (entry.state != SnapshotState::Active || !checksum_matches(entry)) {
      continue;
    }

    if (!best_entry.has_value() || is_better_active_candidate(entry, *best_entry)) {
      best_entry = entry;
    }
  }

  return best_entry;
}

std::optional<VersionLedgerEntry> VersionLedger::last_known_good() const {
  std::optional<VersionLedgerEntry> best_entry;

  for (const auto& entry : entries_) {
    if (!participates_in_last_known_good(entry) || !checksum_matches(entry)) {
      continue;
    }

    if (!best_entry.has_value() || is_better_last_known_good_candidate(entry, *best_entry)) {
      best_entry = entry;
    }
  }

  return best_entry;
}

bool VersionLedger::persist_entries() const {
  if (deps_.ledger_path.empty()) {
    return true;
  }

  try {
    const auto parent_path = deps_.ledger_path.parent_path();
    if (!parent_path.empty()) {
      std::filesystem::create_directories(parent_path);
    }

    std::string payload = serialize_header_line();
    payload.push_back('\n');
    for (const auto& entry : entries_) {
      payload += serialize_entry_line(entry);
      payload.push_back('\n');
    }

    std::string content = payload;
    content += serialize_checksum_line(fnv1a_hex(payload));
    content.push_back('\n');

    auto temporary_path = deps_.ledger_path;
    temporary_path += ".tmp";

    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      return false;
    }

    output << content;
    output.close();
    if (!output) {
      std::error_code cleanup_error;
      std::filesystem::remove(temporary_path, cleanup_error);
      return false;
    }

    std::filesystem::rename(temporary_path, deps_.ledger_path);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool VersionLedger::checksum_matches(const VersionLedgerEntry& entry) const {
  if (entry.checksum.empty()) {
    return false;
  }

  if (!deps_.read_snapshot_checksum) {
    return true;
  }

  try {
    const auto actual_checksum = deps_.read_snapshot_checksum(entry.snapshot_id);
    return actual_checksum.has_value() && *actual_checksum == entry.checksum;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace dasall::knowledge::index
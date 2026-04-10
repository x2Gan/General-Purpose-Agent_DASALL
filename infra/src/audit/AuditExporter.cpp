#include "AuditExporter.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::infra::audit {

namespace {

struct ResumeCursor {
  std::string fingerprint;
  std::int64_t timestamp = 0;
  std::string event_id;

  [[nodiscard]] bool is_valid() const {
    return !fingerprint.empty() && timestamp > 0 && !event_id.empty();
  }
};

void append_string_component(std::string* canonical, std::string_view value) {
  canonical->append(std::to_string(value.size()));
  canonical->push_back('#');
  canonical->append(value);
  canonical->push_back('|');
}

[[nodiscard]] std::string to_hex(std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << value;
  return stream.str();
}

[[nodiscard]] std::string hex_encode(std::string_view value) {
  static constexpr char kHexDigits[] = "0123456789abcdef";

  std::string encoded;
  encoded.reserve(value.size() * 2U);

  for (const unsigned char ch : value) {
    encoded.push_back(kHexDigits[(ch >> 4U) & 0x0FU]);
    encoded.push_back(kHexDigits[ch & 0x0FU]);
  }

  return encoded;
}

[[nodiscard]] std::optional<std::string> hex_decode(std::string_view value) {
  if ((value.size() % 2U) != 0U) {
    return std::nullopt;
  }

  auto decode_nibble = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') {
      return ch - '0';
    }

    if (ch >= 'a' && ch <= 'f') {
      return ch - 'a' + 10;
    }

    if (ch >= 'A' && ch <= 'F') {
      return ch - 'A' + 10;
    }

    return -1;
  };

  std::string decoded;
  decoded.reserve(value.size() / 2U);

  for (std::size_t index = 0; index < value.size(); index += 2U) {
    const int high = decode_nibble(value[index]);
    const int low = decode_nibble(value[index + 1U]);
    if (high < 0 || low < 0) {
      return std::nullopt;
    }

    decoded.push_back(static_cast<char>((high << 4U) | low));
  }

  return decoded;
}

[[nodiscard]] std::string canonicalize_query(const ExportQuery& query) {
  std::string canonical;
  canonical.reserve(128U);

  canonical.append(std::to_string(query.start_ts));
  canonical.push_back('|');
  canonical.append(std::to_string(query.end_ts));
  canonical.push_back('|');
  append_string_component(&canonical, query.actor);
  append_string_component(&canonical, query.action);
  append_string_component(&canonical, query.target);
  canonical.append(std::to_string(static_cast<int>(query.outcome)));

  return canonical;
}

[[nodiscard]] std::string make_filter_fingerprint(const ExportQuery& query) {
  constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

  const auto canonical = canonicalize_query(query);
  std::uint64_t hash = kFnvOffsetBasis;

  for (const unsigned char ch : canonical) {
    hash ^= ch;
    hash *= kFnvPrime;
  }

  return to_hex(hash);
}

[[nodiscard]] std::string make_page_token(const ExportQuery& query,
                                          const AuditEvent& event) {
  return make_filter_fingerprint(query) + "|" +
         std::to_string(event.timestamp) + "|" + hex_encode(event.event_id);
}

[[nodiscard]] std::optional<ResumeCursor> parse_page_token(
    std::string_view page_token) {
  const std::size_t first_delimiter = page_token.find('|');
  if (first_delimiter == std::string_view::npos) {
    return std::nullopt;
  }

  const std::size_t second_delimiter = page_token.find('|', first_delimiter + 1U);
  if (second_delimiter == std::string_view::npos) {
    return std::nullopt;
  }

  ResumeCursor cursor;
  cursor.fingerprint = std::string(page_token.substr(0U, first_delimiter));

  const std::string_view timestamp_view =
      page_token.substr(first_delimiter + 1U,
                        second_delimiter - first_delimiter - 1U);
  const auto parse_result = std::from_chars(
      timestamp_view.data(),
      timestamp_view.data() + timestamp_view.size(),
      cursor.timestamp);
  if (parse_result.ec != std::errc{} ||
      parse_result.ptr != timestamp_view.data() + timestamp_view.size()) {
    return std::nullopt;
  }

  const auto event_id = hex_decode(page_token.substr(second_delimiter + 1U));
  if (!event_id.has_value()) {
    return std::nullopt;
  }

  cursor.event_id = std::move(*event_id);
  if (!cursor.is_valid()) {
    return std::nullopt;
  }

  return cursor;
}

[[nodiscard]] bool matches_query(const AuditEvent& event,
                                 const ExportQuery& query) {
  if (event.timestamp < query.start_ts || event.timestamp > query.end_ts) {
    return false;
  }

  if (!query.actor.empty() && event.actor != query.actor) {
    return false;
  }

  if (!query.action.empty() && event.action != query.action) {
    return false;
  }

  if (!query.target.empty() && event.target != query.target) {
    return false;
  }

  if (query.filters_on_outcome() && event.outcome != query.outcome) {
    return false;
  }

  return true;
}

[[nodiscard]] AuditEvent sanitize_for_export(const AuditEvent& event) {
  return event;
}

[[nodiscard]] bool is_after_cursor(const AuditEvent& event,
                                   const ResumeCursor& cursor) {
  return event.timestamp > cursor.timestamp ||
         (event.timestamp == cursor.timestamp && event.event_id > cursor.event_id);
}

[[nodiscard]] std::string make_export_checksum(
    const std::vector<AuditEvent>& records) {
  if (records.empty()) {
    return "audit-export:empty";
  }

  return std::string("audit-export:") + records.front().event_id + ":" +
         records.back().event_id + ":" + std::to_string(records.size());
}

[[nodiscard]] ExportResult make_empty_export_result() {
  return ExportResult{
      .records = {},
      .next_page_token = std::string(),
      .truncated = false,
      .checksum = make_export_checksum({}),
  };
}

}  // namespace

ExportResult AuditExporter::export_records(const ExportQuery& query) const {
  auto records = collect_matching_records(query);
  std::size_t start_index = 0U;

  if (query.requests_page_resume()) {
    const auto cursor = parse_page_token(query.page_token);
    if (!cursor.has_value() ||
        cursor->fingerprint != make_filter_fingerprint(query)) {
      return make_empty_export_result();
    }

    while (start_index < records.size() &&
           !is_after_cursor(records[start_index], *cursor)) {
      ++start_index;
    }
  }

  std::size_t end_index = records.size();
  std::string next_page_token;
  bool truncated = false;

  if (max_page_size_.has_value() && *max_page_size_ > 0U &&
      records.size() - start_index > *max_page_size_) {
    end_index = start_index + *max_page_size_;
    truncated = true;
    next_page_token = make_page_token(query, records[end_index - 1U]);
  }

  std::vector<AuditEvent> page_records;
  page_records.reserve(end_index - start_index);
  for (std::size_t index = start_index; index < end_index; ++index) {
    page_records.push_back(sanitize_for_export(records[index]));
  }

  return ExportResult{
      .records = std::move(page_records),
      .next_page_token = std::move(next_page_token),
      .truncated = truncated,
      .checksum = make_export_checksum(page_records),
  };
}

std::vector<AuditEvent> AuditExporter::collect_matching_records(
    const ExportQuery& query) const {
  std::vector<AuditEvent> records;

  const auto append_from = [&records, &query](const std::vector<AuditEvent>* source) {
    if (source == nullptr) {
      return;
    }

    std::copy_if(source->begin(),
                 source->end(),
                 std::back_inserter(records),
                 [&query](const AuditEvent& event) { return matches_query(event, query); });
  };

  append_from(primary_records_);
  append_from(fallback_records_);

  std::sort(records.begin(), records.end(), [](const AuditEvent& lhs,
                                               const AuditEvent& rhs) {
    if (lhs.timestamp != rhs.timestamp) {
      return lhs.timestamp < rhs.timestamp;
    }

    return lhs.event_id < rhs.event_id;
  });

  return records;
}

}  // namespace dasall::infra::audit
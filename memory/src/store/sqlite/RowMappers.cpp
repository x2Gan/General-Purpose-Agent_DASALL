#include "store/sqlite/RowMappers.h"

#include <sqlite3.h>

#include <sstream>

namespace dasall::memory::store::sqlite {
namespace {

[[nodiscard]] std::string escape_json_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 4U);
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

[[nodiscard]] std::optional<std::vector<std::string>> decode_optional_string_array(
    sqlite3_stmt* statement,
    int column_index) {
  if (sqlite3_column_type(statement, column_index) == SQLITE_NULL) {
    return std::nullopt;
  }

  return decode_string_array(reinterpret_cast<const char*>(
      sqlite3_column_text(statement, column_index)));
}

}  // namespace

std::string encode_string_array(
    const std::optional<std::vector<std::string>>& values) {
  if (!values.has_value()) {
    return "[]";
  }

  std::ostringstream stream;
  stream << '[';
  for (std::size_t index = 0; index < values->size(); ++index) {
    if (index > 0U) {
      stream << ',';
    }
    stream << '"' << escape_json_string((*values)[index]) << '"';
  }
  stream << ']';
  return stream.str();
}

std::optional<std::string> column_text(sqlite3_stmt* statement, int column_index) {
  if (sqlite3_column_type(statement, column_index) == SQLITE_NULL) {
    return std::nullopt;
  }

  const auto* value = reinterpret_cast<const char*>(
      sqlite3_column_text(statement, column_index));
  return value == nullptr ? std::nullopt : std::optional<std::string>{value};
}

std::optional<std::int64_t> column_int64(sqlite3_stmt* statement, int column_index) {
  if (sqlite3_column_type(statement, column_index) == SQLITE_NULL) {
    return std::nullopt;
  }

  return sqlite3_column_int64(statement, column_index);
}

std::vector<std::string> decode_string_array(const std::string& value) {
  std::vector<std::string> decoded_values;
  if (value.size() < 2U || value == "[]") {
    return decoded_values;
  }

  std::string current;
  bool in_string = false;
  bool escaping = false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char character = value[index];
    if (!in_string) {
      if (character == '"') {
        in_string = true;
        current.clear();
      }
      continue;
    }

    if (escaping) {
      switch (character) {
        case 'n':
          current.push_back('\n');
          break;
        case 'r':
          current.push_back('\r');
          break;
        case 't':
          current.push_back('\t');
          break;
        default:
          current.push_back(character);
          break;
      }
      escaping = false;
      continue;
    }

    if (character == '\\') {
      escaping = true;
      continue;
    }

    if (character == '"') {
      decoded_values.push_back(current);
      in_string = false;
      continue;
    }

    current.push_back(character);
  }

  return decoded_values;
}

contracts::Session map_row_to_session(sqlite3_stmt* statement) {
  contracts::Session session;
  session.session_id = column_text(statement, 0);
  session.user_id = column_text(statement, 1);
  session.latest_summary_memory_ref = column_text(statement, 2);
  session.metadata_digest = column_text(statement, 3);
  session.turn_ids = decode_string_array(
      column_text(statement, 4).value_or("[]"));
  session.created_at = column_int64(statement, 5);
  session.last_active_at = column_int64(statement, 6);

  const auto tags = decode_optional_string_array(statement, 7);
  if (tags.has_value() && !tags->empty()) {
    session.tags = tags;
  }

  return session;
}

contracts::Turn map_row_to_turn(sqlite3_stmt* statement) {
  contracts::Turn turn;
  turn.turn_id = column_text(statement, 0);
  turn.session_id = column_text(statement, 1);
  turn.user_input = column_text(statement, 2);
  turn.agent_response = column_text(statement, 3);

  const auto tool_refs = decode_optional_string_array(statement, 4);
  if (tool_refs.has_value() && !tool_refs->empty()) {
    turn.tool_call_refs = tool_refs;
  }

  const auto observation_refs = decode_optional_string_array(statement, 5);
  if (observation_refs.has_value() && !observation_refs->empty()) {
    turn.observation_refs = observation_refs;
  }

  turn.summary_memory_ref = column_text(statement, 6);
  turn.created_at = column_int64(statement, 7);

  const auto tags = decode_optional_string_array(statement, 8);
  if (tags.has_value() && !tags->empty()) {
    turn.tags = tags;
  }

  return turn;
}

contracts::SummaryMemory map_row_to_summary(sqlite3_stmt* statement) {
  contracts::SummaryMemory summary;
  summary.summary_id = column_text(statement, 0);
  summary.session_id = column_text(statement, 1);
  summary.summary_text = column_text(statement, 2);

  const auto source_turn_ids = decode_optional_string_array(statement, 3);
  if (source_turn_ids.has_value() && !source_turn_ids->empty()) {
    summary.source_turn_ids = source_turn_ids;
  }

  const auto decisions = decode_optional_string_array(statement, 4);
  if (decisions.has_value() && !decisions->empty()) {
    summary.decisions_made = decisions;
  }

  const auto facts = decode_optional_string_array(statement, 5);
  if (facts.has_value() && !facts->empty()) {
    summary.confirmed_facts = facts;
  }

  const auto outcomes = decode_optional_string_array(statement, 6);
  if (outcomes.has_value() && !outcomes->empty()) {
    summary.tool_outcomes = outcomes;
  }

  summary.created_at = column_int64(statement, 7);

  const auto tags = decode_optional_string_array(statement, 8);
  if (tags.has_value() && !tags->empty()) {
    summary.tags = tags;
  }

  return summary;
}

}  // namespace dasall::memory::store::sqlite

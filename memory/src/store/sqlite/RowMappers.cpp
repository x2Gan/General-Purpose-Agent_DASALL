#include "store/sqlite/RowMappers.h"

#include <sqlite3.h>

#include <cstddef>
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

[[nodiscard]] std::optional<std::vector<std::string>> decode_optional_string_array(
    const std::optional<std::string>& encoded_value) {
  if (!encoded_value.has_value() || encoded_value->empty()) {
    return std::nullopt;
  }

  const auto decoded_values = decode_string_array(*encoded_value);
  if (decoded_values.empty()) {
    return std::nullopt;
  }

  return decoded_values;
}

[[nodiscard]] std::optional<std::vector<std::string>> encode_fact_validity_tokens(
    const contracts::MemoryFact& fact) {
  std::vector<std::string> tokens;
  if (fact.valid_until.has_value()) {
    tokens.push_back("valid_until:" + std::to_string(*fact.valid_until));
  }

  if (fact.source_observation_refs.has_value()) {
    for (const auto& observation_ref : *fact.source_observation_refs) {
      tokens.push_back("obs:" + observation_ref);
    }
  }

  if (tokens.empty()) {
    return std::nullopt;
  }

  return tokens;
}

[[nodiscard]] std::optional<std::vector<std::string>> encode_experience_sidecar_tokens(
    const contracts::ExperienceMemory& experience) {
  std::vector<std::string> tokens;
  if (experience.risk_notes.has_value()) {
    tokens.push_back("risk:" + *experience.risk_notes);
  }

  if (experience.source_fact_ids.has_value()) {
    for (const auto& fact_id : *experience.source_fact_ids) {
      tokens.push_back("fact:" + fact_id);
    }
  }

  if (tokens.empty()) {
    return std::nullopt;
  }

  return tokens;
}

void decode_fact_validity_ref(const std::optional<std::string>& encoded_value,
                              contracts::MemoryFact* fact) {
  const auto tokens = decode_optional_string_array(encoded_value);
  if (!tokens.has_value()) {
    return;
  }

  std::vector<std::string> observation_refs;
  for (const auto& token : *tokens) {
    constexpr std::string_view valid_until_prefix = "valid_until:";
    constexpr std::string_view observation_prefix = "obs:";
    if (token.rfind(valid_until_prefix, 0) == 0) {
      fact->valid_until = std::stoll(token.substr(valid_until_prefix.size()));
      continue;
    }

    if (token.rfind(observation_prefix, 0) == 0) {
      observation_refs.push_back(token.substr(observation_prefix.size()));
    }
  }

  if (!observation_refs.empty()) {
    fact->source_observation_refs = observation_refs;
  }
}

void decode_experience_risk_notes_json(const std::optional<std::string>& encoded_value,
                                       contracts::ExperienceMemory* experience) {
  const auto tokens = decode_optional_string_array(encoded_value);
  if (!tokens.has_value()) {
    return;
  }

  std::vector<std::string> source_fact_ids;
  for (const auto& token : *tokens) {
    constexpr std::string_view risk_prefix = "risk:";
    constexpr std::string_view fact_prefix = "fact:";
    if (token.rfind(risk_prefix, 0) == 0) {
      experience->risk_notes = token.substr(risk_prefix.size());
      continue;
    }

    if (token.rfind(fact_prefix, 0) == 0) {
      source_fact_ids.push_back(token.substr(fact_prefix.size()));
    }
  }

  if (!source_fact_ids.empty()) {
    experience->source_fact_ids = source_fact_ids;
  }
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

std::optional<std::string> encode_fact_validity_ref(
    const contracts::MemoryFact& fact) {
  const auto tokens = encode_fact_validity_tokens(fact);
  if (!tokens.has_value()) {
    return std::nullopt;
  }

  return encode_string_array(tokens);
}

std::string encode_experience_risk_notes_json(
    const contracts::ExperienceMemory& experience) {
  return encode_string_array(encode_experience_sidecar_tokens(experience));
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

contracts::MemoryFact map_row_to_fact(sqlite3_stmt* statement) {
  contracts::MemoryFact fact;
  fact.fact_id = column_text(statement, 0);
  fact.session_id = column_text(statement, 1);
  fact.fact_text = column_text(statement, 3);
  fact.source_turn_ids = decode_string_array(column_text(statement, 4).value_or("[]"));

  if (const auto confidence_score = column_int64(statement, 5); confidence_score.has_value()) {
    fact.confidence_score = static_cast<std::uint32_t>(*confidence_score);
  }

  fact.fact_type = column_text(statement, 6);
  decode_fact_validity_ref(column_text(statement, 7), &fact);
  fact.evidence_digest = column_text(statement, 8);
  fact.superseded_by_fact_id = column_text(statement, 9);
  fact.created_at = column_int64(statement, 10);

  const auto tags = decode_optional_string_array(statement, 11);
  if (tags.has_value() && !tags->empty()) {
    fact.tags = tags;
  }

  return fact;
}

contracts::ExperienceMemory map_row_to_experience(sqlite3_stmt* statement) {
  contracts::ExperienceMemory experience;
  experience.experience_id = column_text(statement, 0);
  experience.session_id = column_text(statement, 1);
  experience.lesson_summary = column_text(statement, 3);
  experience.trigger_condition = column_text(statement, 4);
  experience.recommended_action = column_text(statement, 5);

  const auto source_turn_ids = decode_optional_string_array(statement, 6);
  if (source_turn_ids.has_value() && !source_turn_ids->empty()) {
    experience.source_turn_ids = source_turn_ids;
  }

  if (const auto effectiveness_score = column_int64(statement, 7);
      effectiveness_score.has_value() && *effectiveness_score > 0) {
    experience.effectiveness_score = static_cast<std::uint32_t>(*effectiveness_score);
  }

  const auto applicable_domains = decode_optional_string_array(statement, 8);
  if (applicable_domains.has_value() && !applicable_domains->empty()) {
    experience.applicable_domains = applicable_domains;
  }

  decode_experience_risk_notes_json(column_text(statement, 9), &experience);
  experience.expires_at = column_int64(statement, 10);
  experience.superseded_by_experience_id = column_text(statement, 11);
  experience.created_at = column_int64(statement, 12);

  const auto tags = decode_optional_string_array(statement, 13);
  if (tags.has_value() && !tags->empty()) {
    experience.tags = tags;
  }

  return experience;
}

}  // namespace dasall::memory::store::sqlite

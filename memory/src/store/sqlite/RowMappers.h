#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "memory/Session.h"
#include "memory/SummaryMemory.h"
#include "memory/Turn.h"
#include "memory/MemoryFact.h"
#include "memory/ExperienceMemory.h"

struct sqlite3_stmt;

namespace dasall::memory::store::sqlite {

[[nodiscard]] std::string encode_string_array(
    const std::optional<std::vector<std::string>>& values);

[[nodiscard]] std::optional<std::string> column_text(sqlite3_stmt* statement,
                                                     int column_index);
[[nodiscard]] std::optional<std::int64_t> column_int64(sqlite3_stmt* statement,
                                                       int column_index);
[[nodiscard]] std::vector<std::string> decode_string_array(const std::string& value);
[[nodiscard]] std::optional<std::string> encode_fact_validity_ref(
    const contracts::MemoryFact& fact);
[[nodiscard]] std::string encode_experience_risk_notes_json(
    const contracts::ExperienceMemory& experience);

[[nodiscard]] contracts::Session map_row_to_session(sqlite3_stmt* statement);
[[nodiscard]] contracts::Turn map_row_to_turn(sqlite3_stmt* statement);
[[nodiscard]] contracts::SummaryMemory map_row_to_summary(sqlite3_stmt* statement);
[[nodiscard]] contracts::MemoryFact map_row_to_fact(sqlite3_stmt* statement);
[[nodiscard]] contracts::ExperienceMemory map_row_to_experience(sqlite3_stmt* statement);

}  // namespace dasall::memory::store::sqlite

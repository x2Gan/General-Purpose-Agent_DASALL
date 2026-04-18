#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "memory/Session.h"
#include "memory/SummaryMemory.h"
#include "memory/Turn.h"

struct sqlite3_stmt;

namespace dasall::memory::store::sqlite {

[[nodiscard]] std::string encode_string_array(
    const std::optional<std::vector<std::string>>& values);

[[nodiscard]] std::optional<std::string> column_text(sqlite3_stmt* statement,
                                                     int column_index);
[[nodiscard]] std::optional<std::int64_t> column_int64(sqlite3_stmt* statement,
                                                       int column_index);
[[nodiscard]] std::vector<std::string> decode_string_array(const std::string& value);

[[nodiscard]] contracts::Session map_row_to_session(sqlite3_stmt* statement);
[[nodiscard]] contracts::Turn map_row_to_turn(sqlite3_stmt* statement);
[[nodiscard]] contracts::SummaryMemory map_row_to_summary(sqlite3_stmt* statement);

}  // namespace dasall::memory::store::sqlite

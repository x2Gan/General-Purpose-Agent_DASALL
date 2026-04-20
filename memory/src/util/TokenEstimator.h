#pragma once

#include <algorithm>
#include <string_view>

namespace dasall::memory::util {

[[nodiscard]] inline int estimate_text_tokens(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  int ascii_bytes = 0;
  int multibyte_characters = 0;

  for (const unsigned char byte : text) {
    if (byte < 0x80U) {
      ++ascii_bytes;
      continue;
    }

    if ((byte & 0xC0U) != 0x80U) {
      ++multibyte_characters;
    }
  }

  return std::max(1, ((ascii_bytes + 3) / 4) + (multibyte_characters * 2));
}

}  // namespace dasall::memory::util

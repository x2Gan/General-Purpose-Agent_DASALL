#include "TokenEstimator.h"

#include <cmath>
#include <cstddef>
#include <string_view>

namespace {

struct CharacterCounts {
  std::uint32_t ascii_chars = 0;
  std::uint32_t cjk_chars = 0;
  std::uint32_t other_chars = 0;
};

bool is_continuation_byte(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

bool decode_next_utf8(std::string_view text, std::size_t& index, char32_t& code_point) {
  const unsigned char first = static_cast<unsigned char>(text[index]);

  if ((first & 0x80U) == 0U) {
    code_point = static_cast<char32_t>(first);
    ++index;
    return true;
  }

  const std::size_t remaining = text.size() - index;
  if ((first & 0xE0U) == 0xC0U && remaining >= 2U) {
    const unsigned char second = static_cast<unsigned char>(text[index + 1U]);
    if (is_continuation_byte(second)) {
      code_point = (static_cast<char32_t>(first & 0x1FU) << 6U) |
                   static_cast<char32_t>(second & 0x3FU);
      index += 2U;
      return true;
    }
  }

  if ((first & 0xF0U) == 0xE0U && remaining >= 3U) {
    const unsigned char second = static_cast<unsigned char>(text[index + 1U]);
    const unsigned char third = static_cast<unsigned char>(text[index + 2U]);
    if (is_continuation_byte(second) && is_continuation_byte(third)) {
      code_point = (static_cast<char32_t>(first & 0x0FU) << 12U) |
                   (static_cast<char32_t>(second & 0x3FU) << 6U) |
                   static_cast<char32_t>(third & 0x3FU);
      index += 3U;
      return true;
    }
  }

  if ((first & 0xF8U) == 0xF0U && remaining >= 4U) {
    const unsigned char second = static_cast<unsigned char>(text[index + 1U]);
    const unsigned char third = static_cast<unsigned char>(text[index + 2U]);
    const unsigned char fourth = static_cast<unsigned char>(text[index + 3U]);
    if (is_continuation_byte(second) && is_continuation_byte(third) &&
        is_continuation_byte(fourth)) {
      code_point = (static_cast<char32_t>(first & 0x07U) << 18U) |
                   (static_cast<char32_t>(second & 0x3FU) << 12U) |
                   (static_cast<char32_t>(third & 0x3FU) << 6U) |
                   static_cast<char32_t>(fourth & 0x3FU);
      index += 4U;
      return true;
    }
  }

  code_point = 0xFFFD;
  ++index;
  return false;
}

bool is_cjk_code_point(char32_t code_point) {
  return (code_point >= 0x3400 && code_point <= 0x4DBF) ||
         (code_point >= 0x4E00 && code_point <= 0x9FFF) ||
         (code_point >= 0xF900 && code_point <= 0xFAFF) ||
         (code_point >= 0x20000 && code_point <= 0x2A6DF) ||
         (code_point >= 0x2A700 && code_point <= 0x2B73F) ||
         (code_point >= 0x2B740 && code_point <= 0x2B81F) ||
         (code_point >= 0x2B820 && code_point <= 0x2CEAF) ||
         (code_point >= 0x2F800 && code_point <= 0x2FA1F);
}

CharacterCounts count_characters(std::string_view text) {
  CharacterCounts counts;
  std::size_t index = 0U;

  while (index < text.size()) {
    char32_t code_point = 0;
    decode_next_utf8(text, index, code_point);

    if (code_point <= 0x7F) {
      ++counts.ascii_chars;
      continue;
    }

    if (is_cjk_code_point(code_point)) {
      ++counts.cjk_chars;
      continue;
    }

    ++counts.other_chars;
  }

  return counts;
}

std::uint32_t estimate_tokens(std::string_view text,
                              const dasall::llm::TokenEstimatorConfig& config) {
  const CharacterCounts counts = count_characters(text);
  const double raw_tokens = static_cast<double>(counts.ascii_chars) / config.english_chars_per_token +
                            static_cast<double>(counts.cjk_chars) / config.cjk_chars_per_token +
                            static_cast<double>(counts.other_chars) / config.other_chars_per_token;

  if (raw_tokens <= 0.0) {
    return 0U;
  }

  return static_cast<std::uint32_t>(
      std::ceil(raw_tokens * (1.0 + config.safety_margin)));
}

dasall::llm::TokenEstimate make_token_estimate(std::uint32_t estimated_input_tokens,
                                               std::uint32_t context_window,
                                               std::uint32_t reserved_output_tokens,
                                               double safety_margin) {
  return dasall::llm::TokenEstimate{
      .estimated_input_tokens = estimated_input_tokens,
      .reserved_output_tokens = reserved_output_tokens,
      .context_window = context_window,
      .over_budget = estimated_input_tokens + reserved_output_tokens > context_window,
      .safety_margin = safety_margin,
  };
}

}  // namespace

namespace dasall::llm {

TokenEstimator::TokenEstimator(TokenEstimatorConfig config)
    : config_(config.has_consistent_values() ? config : TokenEstimatorConfig{}) {}

TokenEstimate TokenEstimator::estimate(std::string_view text,
                                       std::uint32_t context_window,
                                       std::uint32_t reserved_output_tokens) const {
  return make_token_estimate(estimate_tokens(text, config_), context_window,
                             reserved_output_tokens, config_.safety_margin);
}

TokenEstimate TokenEstimator::estimate(const std::vector<std::string>& messages,
                                       std::uint32_t context_window,
                                       std::uint32_t reserved_output_tokens) const {
  std::uint32_t estimated_input_tokens = 0U;
  for (const auto& message : messages) {
    estimated_input_tokens += estimate_tokens(message, config_);
  }

  return make_token_estimate(estimated_input_tokens, context_window,
                             reserved_output_tokens, config_.safety_margin);
}

}  // namespace dasall::llm
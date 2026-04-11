#include "TemplateRenderer.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

bool is_continuation_byte(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

bool decode_next_utf8(std::string_view text, std::size_t& index) {
  const unsigned char first = static_cast<unsigned char>(text[index]);

  if ((first & 0x80U) == 0U) {
    ++index;
    return true;
  }

  const std::size_t remaining = text.size() - index;
  if ((first & 0xE0U) == 0xC0U && remaining >= 2U) {
    const unsigned char second = static_cast<unsigned char>(text[index + 1U]);
    if (is_continuation_byte(second)) {
      index += 2U;
      return true;
    }
  }

  if ((first & 0xF0U) == 0xE0U && remaining >= 3U) {
    const unsigned char second = static_cast<unsigned char>(text[index + 1U]);
    const unsigned char third = static_cast<unsigned char>(text[index + 2U]);
    if (is_continuation_byte(second) && is_continuation_byte(third)) {
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
      index += 4U;
      return true;
    }
  }

  ++index;
  return false;
}

bool is_valid_variable_name(std::string_view variable_name) {
  if (variable_name.empty() || variable_name.size() > 64U) {
    return false;
  }

  for (const unsigned char character : variable_name) {
    if (std::isalnum(character) || character == '_') {
      continue;
    }

    return false;
  }

  return true;
}

bool contains_template_delimiter(std::string_view value) {
  return value.find("{{") != std::string_view::npos ||
         value.find("}}") != std::string_view::npos;
}

std::string escape_template_delimiters(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (std::size_t index = 0U; index < value.size();) {
    if (index + 1U < value.size() && value[index] == '{' && value[index + 1U] == '{') {
      escaped.append("\\{\\{");
      index += 2U;
      continue;
    }

    if (index + 1U < value.size() && value[index] == '}' && value[index + 1U] == '}') {
      escaped.append("\\}\\}");
      index += 2U;
      continue;
    }

    escaped.push_back(value[index]);
    ++index;
  }

  return escaped;
}

std::size_t count_utf8_code_points(std::string_view text) {
  std::size_t code_points = 0U;
  std::size_t index = 0U;

  while (index < text.size()) {
    decode_next_utf8(text, index);
    ++code_points;
  }

  return code_points;
}

std::string truncate_utf8(std::string_view text, std::size_t max_code_points) {
  if (max_code_points == 0U) {
    return std::string();
  }

  std::size_t index = 0U;
  std::size_t code_points = 0U;
  while (index < text.size() && code_points < max_code_points) {
    decode_next_utf8(text, index);
    ++code_points;
  }

  return std::string(text.substr(0U, index));
}

}  // namespace

namespace dasall::llm::prompt {

bool TemplateRenderer::init(const TemplateRendererConfig& config) {
  if (!config.has_consistent_values()) {
    config_ = TemplateRendererConfig{};
    initialized_ = false;
    return false;
  }

  config_ = config;
  initialized_ = true;
  return true;
}

TemplateRenderResult TemplateRenderer::render(
    std::string_view template_text,
    const TemplateVariables& variables) const {
  TemplateRenderResult result;

  if (!initialized_) {
    result.rendered_text = std::string(template_text);
    result.warnings.push_back("renderer_not_initialized");
    return result;
  }

  result.rendered_text.reserve(template_text.size());
  std::size_t cursor = 0U;

  while (cursor < template_text.size()) {
    const std::size_t open = template_text.find("{{", cursor);
    if (open == std::string_view::npos) {
      result.rendered_text.append(template_text.substr(cursor));
      break;
    }

    result.rendered_text.append(template_text.substr(cursor, open - cursor));

    const std::size_t close = template_text.find("}}", open + 2U);
    if (close == std::string_view::npos) {
      result.rendered_text.append(template_text.substr(open));
      break;
    }

    const std::string_view placeholder = template_text.substr(open, close + 2U - open);
    const std::string_view variable_name = template_text.substr(open + 2U, close - open - 2U);

    if (!is_valid_variable_name(variable_name)) {
      result.rendered_text.append(placeholder);
      result.warnings.push_back("unsupported_template_tag:" + std::string(variable_name));
      cursor = close + 2U;
      continue;
    }

    const auto variable = variables.find(std::string(variable_name));
    if (variable == variables.end()) {
      result.rendered_text.append(placeholder);
      result.warnings.push_back("unmatched_variable:" + std::string(variable_name));
      cursor = close + 2U;
      continue;
    }

    std::string rendered_value = variable->second;
    if (contains_template_delimiter(rendered_value)) {
      rendered_value = escape_template_delimiters(rendered_value);
      result.nested_render_rejected = true;
      result.warnings.push_back("nested_render_rejected:" + std::string(variable_name));
    }

    if (count_utf8_code_points(rendered_value) > config_.max_variable_length) {
      rendered_value = truncate_utf8(rendered_value, config_.max_variable_length);
      result.truncated_values = true;
      result.warnings.push_back("value_truncated:" + std::string(variable_name));
    }

    result.rendered_text.append(rendered_value);
    cursor = close + 2U;
  }

  return result;
}

}  // namespace dasall::llm::prompt
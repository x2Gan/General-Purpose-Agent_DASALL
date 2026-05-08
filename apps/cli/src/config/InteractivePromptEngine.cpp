#include "config/InteractivePromptEngine.h"

namespace dasall::apps::cli::config {

namespace {

[[nodiscard]] PromptResponse make_default_response(std::string_view default_value,
                                                   bool masked) {
  if (default_value.empty()) {
    return PromptResponse{
        .accepted = false,
        .used_default = false,
        .masked = masked,
        .value = {},
    };
  }

  return PromptResponse{
      .accepted = true,
      .used_default = true,
      .masked = masked,
      .value = std::string(default_value),
  };
}

}  // namespace

InteractivePromptEngine::InteractivePromptEngine(InputHandler input_handler,
                                                 ConfirmHandler confirm_handler)
    : input_handler_(std::move(input_handler)),
      confirm_handler_(std::move(confirm_handler)) {}

PromptResponse InteractivePromptEngine::prompt_text(
    std::string_view field_name,
    std::string_view message,
    std::string_view default_value) const {
  return prompt_value(field_name, message, default_value, false);
}

PromptResponse InteractivePromptEngine::prompt_secret(
    std::string_view field_name,
    std::string_view message) const {
  return prompt_value(field_name, message, {}, true);
}

bool InteractivePromptEngine::prompt_confirm(std::string_view message,
                                             const bool default_value) const {
  if (!confirm_handler_) {
    return default_value;
  }

  const auto result = confirm_handler_(ConfirmRequest{
      .message = std::string(message),
      .default_value = default_value,
  });

  return result.value_or(default_value);
}

PromptResponse InteractivePromptEngine::prompt_value(
    std::string_view field_name,
    std::string_view message,
    std::string_view default_value,
    const bool masked) const {
  if (!input_handler_) {
    return make_default_response(default_value, masked);
  }

  const auto raw_value = input_handler_(PromptRequest{
      .field_name = std::string(field_name),
      .message = std::string(message),
      .default_value = std::string(default_value),
      .masked = masked,
  });
  if (!raw_value.has_value()) {
    return make_default_response(default_value, masked);
  }

  if (raw_value->empty()) {
    return make_default_response(default_value, masked);
  }

  return PromptResponse{
      .accepted = true,
      .used_default = false,
      .masked = masked,
      .value = *raw_value,
  };
}

}  // namespace dasall::apps::cli::config
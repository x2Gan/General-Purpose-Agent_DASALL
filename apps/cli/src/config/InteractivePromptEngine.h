#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace dasall::apps::cli::config {

struct PromptRequest {
  std::string field_name;
  std::string message;
  std::string default_value;
  bool masked = false;
};

struct PromptResponse {
  bool accepted = false;
  bool used_default = false;
  bool masked = false;
  std::string value;
};

struct ConfirmRequest {
  std::string message;
  bool default_value = false;
};

class InteractivePromptEngine {
 public:
  using InputHandler =
      std::function<std::optional<std::string>(const PromptRequest&)>;
  using ConfirmHandler =
      std::function<std::optional<bool>(const ConfirmRequest&)>;

  explicit InteractivePromptEngine(InputHandler input_handler = {},
                                   ConfirmHandler confirm_handler = {});

  [[nodiscard]] PromptResponse prompt_text(std::string_view field_name,
                                           std::string_view message,
                                           std::string_view default_value = {})
      const;

  [[nodiscard]] PromptResponse prompt_secret(std::string_view field_name,
                                             std::string_view message) const;

  [[nodiscard]] bool prompt_confirm(std::string_view message,
                                    bool default_value) const;

 private:
  [[nodiscard]] PromptResponse prompt_value(std::string_view field_name,
                                            std::string_view message,
                                            std::string_view default_value,
                                            bool masked) const;

  InputHandler input_handler_;
  ConfirmHandler confirm_handler_;
};

}  // namespace dasall::apps::cli::config
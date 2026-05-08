#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "config/ConfigCommandTypes.h"
#include "config/InteractivePromptEngine.h"
#include "secret/SecretBootstrapWriter.h"

namespace dasall::apps::cli::config {

struct LlmSecretPageResult {
  bool success = true;
  std::vector<std::string> written_secret_refs;
  std::vector<std::string> blocked_actions;
  std::string error_message;

  [[nodiscard]] bool succeeded() const;
};

class LlmSecretPage {
 public:
  using StdinReader = std::function<std::optional<std::string>()>;

  LlmSecretPage();

  explicit LlmSecretPage(InteractivePromptEngine prompt_engine,
                         StdinReader stdin_reader = {});

  [[nodiscard]] LlmSecretPageResult collect_and_apply(
      const DesiredSecretSettings& desired_settings,
      const dasall::infra::secret::SecretBootstrapWriter& writer) const;

 private:
  InteractivePromptEngine prompt_engine_;
  StdinReader stdin_reader_;
};

}  // namespace dasall::apps::cli::config
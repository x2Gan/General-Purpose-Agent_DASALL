#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "vector/IEmbeddingAdapter.h"

namespace dasall::infra::secret {

class ISecretManager;

}  // namespace dasall::infra::secret

namespace dasall::llm {

class ILLMTransport;

}  // namespace dasall::llm

namespace dasall::apps::runtime_support {

class LLMBackedEmbeddingAdapter final : public memory::IEmbeddingAdapter {
 public:
  struct ProviderConfig {
    std::string provider_id;
    std::string model_id;
    std::string base_url;
    std::string auth_ref;
    std::string base_url_alias;
    std::string snapshot_version;
    std::uint32_t timeout_ms = 15000U;

    [[nodiscard]] bool has_consistent_values() const {
      return !provider_id.empty() && !model_id.empty() && !base_url.empty() &&
             !auth_ref.empty() && !base_url_alias.empty() &&
             !snapshot_version.empty() && timeout_ms > 0U;
    }
  };

  struct Options {
    ProviderConfig provider;
    std::string composition_owner = "runtime.memory.embedding";
    int expected_dimension = 0;
  };

  LLMBackedEmbeddingAdapter(
      std::shared_ptr<llm::ILLMTransport> transport,
      std::shared_ptr<infra::secret::ISecretManager> secret_manager,
      Options options);

  [[nodiscard]] std::vector<float> embed(const std::string& text) const override;
  [[nodiscard]] int dimension() const override;

 private:
  std::shared_ptr<llm::ILLMTransport> transport_;
  std::shared_ptr<infra::secret::ISecretManager> secret_manager_;
  Options options_;
  mutable std::mutex mutex_;
  mutable int dimension_ = 0;
};

}  // namespace dasall::apps::runtime_support
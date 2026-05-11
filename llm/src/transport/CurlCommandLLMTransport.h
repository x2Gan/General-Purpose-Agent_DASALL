#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "ILLMTransport.h"

namespace dasall::infra::secret {

class ISecretBackend;

}  // namespace dasall::infra::secret

namespace dasall::llm::transport {

struct CurlCommandLLMTransportOptions {
  std::filesystem::path curl_path = "/usr/bin/curl";
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
  std::shared_ptr<infra::secret::ISecretBackend> secret_backend;
  std::string actor = "dasall-daemon";
};

class CurlCommandLLMTransport final : public ILLMTransport {
 public:
  explicit CurlCommandLLMTransport(CurlCommandLLMTransportOptions options);

  [[nodiscard]] LLMTransportResponse send(const LLMTransportRequest& request) override;

 private:
  CurlCommandLLMTransportOptions options_;
};

}  // namespace dasall::llm::transport
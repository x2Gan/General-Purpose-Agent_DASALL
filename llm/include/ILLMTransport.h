#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace dasall::llm {

enum class LLMTransportMethod {
  Get = 0,
  Post = 1,
};

struct LLMTransportHeader {
  std::string name;
  std::string value;

  [[nodiscard]] bool has_consistent_values() const {
    return !name.empty() && !value.empty();
  }
};

struct LLMTransportRequest {
  LLMTransportMethod method = LLMTransportMethod::Get;
  std::string url;
  std::string auth_ref;
  std::vector<std::string> header_refs;
  std::string base_url_alias;
  std::string snapshot_version;
  std::vector<LLMTransportHeader> headers;
  std::string body;
  std::uint32_t timeout_ms = 0U;

  [[nodiscard]] bool has_consistent_values() const {
    if (url.empty() || auth_ref.empty() || base_url_alias.empty() ||
        snapshot_version.empty() || timeout_ms == 0U) {
      return false;
    }

    if (method == LLMTransportMethod::Post && body.empty()) {
      return false;
    }

    if (!std::all_of(headers.begin(), headers.end(),
                     [](const LLMTransportHeader& header) {
                       return header.has_consistent_values();
                     })) {
      return false;
    }

    std::vector<std::string> sorted_refs = header_refs;
    std::sort(sorted_refs.begin(), sorted_refs.end());
    if (std::adjacent_find(sorted_refs.begin(), sorted_refs.end()) != sorted_refs.end()) {
      return false;
    }

    return std::all_of(header_refs.begin(), header_refs.end(), [](const std::string& value) {
      return !value.empty();
    });
  }
};

struct LLMTransportResponse {
  std::uint16_t status_code = 0U;
  std::string body;
  std::string error_message;

  [[nodiscard]] bool has_consistent_values() const {
    if (status_code == 0U) {
      return !error_message.empty();
    }

    return status_code >= 100U && status_code <= 599U;
  }

  [[nodiscard]] bool ok() const {
    return has_consistent_values() && error_message.empty() && status_code >= 200U &&
           status_code < 300U;
  }
};

class ILLMTransport {
 public:
  virtual ~ILLMTransport() = default;

  [[nodiscard]] virtual LLMTransportResponse send(
      const LLMTransportRequest& request) = 0;
};

}  // namespace dasall::llm
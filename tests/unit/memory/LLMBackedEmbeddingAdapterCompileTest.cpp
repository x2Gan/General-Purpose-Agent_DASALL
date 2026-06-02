#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ILLMTransport.h"
#include "LLMBackedEmbeddingAdapter.h"
#include "support/TestAssertions.h"

namespace {

class RecordingTransport final : public dasall::llm::ILLMTransport {
 public:
  [[nodiscard]] dasall::llm::LLMTransportResponse send(
      const dasall::llm::LLMTransportRequest& request) override {
    ++send_calls_;
    last_request_ = request;
    return dasall::llm::LLMTransportResponse{
        .status_code = 200U,
        .body =
            R"({"data":[{"embedding":[0.25,-0.5,0.75],"index":0}],"model":"deepseek-embedding"})",
        .error_message = {},
    };
  }

  int send_calls_ = 0;
  std::optional<dasall::llm::LLMTransportRequest> last_request_;
};

[[nodiscard]] std::optional<std::string> header_value(
    const dasall::llm::LLMTransportRequest& request,
    const std::string& header_name) {
  for (const auto& header : request.headers) {
    if (header.name == header_name) {
      return header.value;
    }
  }

  return std::nullopt;
}

void test_llm_backed_embedding_adapter_projects_transport_request_and_embedding() {
  using dasall::tests::support::assert_true;

  auto transport = std::make_shared<RecordingTransport>();

  dasall::apps::runtime_support::LLMBackedEmbeddingAdapter::Options options;
  options.provider =
      dasall::apps::runtime_support::LLMBackedEmbeddingAdapter::ProviderConfig{
          .provider_id = "deepseek-prod",
          .model_id = "deepseek-embedding",
          .base_url = "https://embedding.example/v1/",
          .auth_ref = "profile://embedding.default",
          .base_url_alias = "deepseek.prod",
          .snapshot_version = "provider-snapshot@2026.06.02",
          .timeout_ms = 12000U,
      };
  options.composition_owner = "daemon.local-control-plane:memory.embedding";

  dasall::apps::runtime_support::LLMBackedEmbeddingAdapter adapter(
      transport,
      nullptr,
      std::move(options));

  const auto embedding = adapter.embed("memory recall query");

  assert_true(embedding.size() == 3U && embedding[0] == 0.25F &&
                  embedding[1] == -0.5F && embedding[2] == 0.75F,
              "LLM-backed embedding adapter compile test should parse the provider embedding payload");
  assert_true(adapter.dimension() == 3,
              "LLM-backed embedding adapter compile test should cache the resolved embedding dimension");
  assert_true(transport->send_calls_ == 1,
              "LLM-backed embedding adapter compile test should issue exactly one transport request");
  assert_true(transport->last_request_.has_value(),
              "LLM-backed embedding adapter compile test should retain the outgoing transport request");

  const auto& request = *transport->last_request_;
  assert_true(request.url == "https://embedding.example/v1/embeddings",
              "LLM-backed embedding adapter compile test should normalize the provider base URL before appending /embeddings");
  assert_true(request.auth_ref == "profile://embedding.default",
              "LLM-backed embedding adapter compile test should preserve auth_ref for transport-side traceability");
  assert_true(request.base_url_alias == "deepseek.prod" &&
                  request.snapshot_version == "provider-snapshot@2026.06.02",
              "LLM-backed embedding adapter compile test should project provider alias and snapshot version onto the transport request");
  assert_true(request.timeout_ms == 12000U,
              "LLM-backed embedding adapter compile test should carry the configured timeout into the transport request");
  assert_true(header_value(request, "Content-Type") ==
                  std::optional<std::string>{"application/json"},
              "LLM-backed embedding adapter compile test should request JSON content");
  assert_true(header_value(request, "Accept") ==
                  std::optional<std::string>{"application/json"},
              "LLM-backed embedding adapter compile test should accept JSON responses");
  assert_true(!header_value(request, "Authorization").has_value(),
              "LLM-backed embedding adapter compile test should not inject a bearer header for profile:// auth refs");
  assert_true(request.body.find("\"model\":\"deepseek-embedding\"") !=
                  std::string::npos &&
                  request.body.find("\"input\":\"memory recall query\"") !=
                      std::string::npos,
              "LLM-backed embedding adapter compile test should serialize model and input into the provider payload");
}

}  // namespace

int main() {
  try {
    test_llm_backed_embedding_adapter_projects_transport_request_and_embedding();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
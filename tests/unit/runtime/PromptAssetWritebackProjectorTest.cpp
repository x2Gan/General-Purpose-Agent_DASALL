#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "ILLMManager.h"
#include "LLMManagerResult.h"
#include "PromptAssetWritebackProjector.h"
#include "support/TestAssertions.h"

namespace {

class StubLLMManager final : public dasall::llm::ILLMManager {
 public:
  bool init(const dasall::llm::LLMSubsystemConfig& config) override {
    (void)config;
    return true;
  }

  dasall::llm::LLMManagerResult generate(
      const dasall::llm::LLMGenerateRequest& request) override {
    (void)request;
    return {};
  }

  dasall::llm::LLMManagerResult stream_generate(
      const dasall::llm::LLMGenerateRequest& request,
      dasall::llm::IStreamObserver* observer) override {
    (void)request;
    (void)observer;
    return {};
  }

  [[nodiscard]] std::optional<dasall::llm::prompt::PromptAssetMetadata>
  lookup_prompt_asset_metadata(std::string_view prompt_release_id) const override {
    if (prompt_release_id != "responder@2026.06.23") {
      return std::nullopt;
    }

    return dasall::llm::prompt::PromptAssetMetadata{
        .prompt_release_id = "responder@2026.06.23",
        .package_id = "responder.programmatic",
        .content_hash = "sha256:runtime-prompt-asset",
        .source_layer = "baseline",
        .source_uri = "prompt://responder/programmatic",
    };
  }

  [[nodiscard]] bool abandon_call(std::string_view llm_call_id) override {
    (void)llm_call_id;
    return false;
  }

  dasall::llm::HealthStatus health_check() const override {
    return {.ready = true, .degraded = false, .message = "ready"};
  }
};

void test_prompt_asset_writeback_projector_projects_llm_asset_metadata() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::contracts::AgentRequest request;
  request.request_id = "req-runtime-asset";
  request.session_id = "session-runtime-asset";
  request.created_at = 123456;

  dasall::memory::MemoryWritebackRequest writeback_request;
  writeback_request.session_id = "session-runtime-asset";
  writeback_request.turn.turn_id = "turn-runtime-asset";

  dasall::contracts::LLMResponse response;
  response.prompt_id = "responder";
  response.prompt_version = "2026.06.23";

  dasall::llm::LLMManagerResult llm_result;
  llm_result.response = response;
  llm_result.resolved_route = "cloud.general";

  const auto projected = dasall::runtime::PromptAssetWritebackProjector{}
                             .enrich_with_prompt_asset(
                                 std::move(writeback_request),
                                 request,
                                 llm_result,
                                 std::make_shared<StubLLMManager>());

  assert_equal(1, static_cast<int>(projected.programmatic_candidates.size()),
               "prompt asset projector should append one programmatic candidate for a known prompt release");
  assert_equal(std::string{"prompt:responder@2026.06.23"},
               projected.programmatic_candidates.front().record.asset_ref,
               "prompt asset projector should normalize the asset ref with a prompt: prefix");
  assert_equal(std::string{"sha256:runtime-prompt-asset"},
               projected.programmatic_candidates.front().record.content_digest,
               "prompt asset projector should preserve the llm-provided content digest");
  assert_true(projected.programmatic_candidates.front().record.lease_expires_at > 123456,
              "prompt asset projector should allocate a future lease expiry window");
}

void test_prompt_asset_writeback_projector_skips_unknown_prompt_release() {
  using dasall::tests::support::assert_equal;

  dasall::contracts::AgentRequest request;
  request.request_id = "req-runtime-asset-miss";
  request.session_id = "session-runtime-asset-miss";
  request.created_at = 123456;

  dasall::memory::MemoryWritebackRequest writeback_request;
  writeback_request.session_id = "session-runtime-asset-miss";
  writeback_request.turn.turn_id = "turn-runtime-asset-miss";

  dasall::contracts::LLMResponse response;
  response.prompt_id = "responder";
  response.prompt_version = "missing";

  dasall::llm::LLMManagerResult llm_result;
  llm_result.response = response;
  llm_result.resolved_route = "cloud.general";

  const auto projected = dasall::runtime::PromptAssetWritebackProjector{}
                             .enrich_with_prompt_asset(
                                 std::move(writeback_request),
                                 request,
                                 llm_result,
                                 std::make_shared<StubLLMManager>());

  assert_equal(0, static_cast<int>(projected.programmatic_candidates.size()),
               "prompt asset projector should skip unknown prompt releases");
}

}  // namespace

int main() {
  try {
    test_prompt_asset_writeback_projector_projects_llm_asset_metadata();
    test_prompt_asset_writeback_projector_skips_unknown_prompt_release();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/provider/ProviderCatalogRepository.h"

namespace {

void test_repository_parses_provider_model_metadata_for_budget_and_pricing() {
  using dasall::llm::ProviderCatalogSourceConfig;
  using dasall::llm::provider::ProviderCatalogRepository;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ProviderCatalogRepository repository;
  ProviderCatalogSourceConfig config;
  config.baseline_root =
      (std::filesystem::path(DASALL_REPO_ROOT) / "llm/assets/providers").generic_string();

  assert_true(repository.init(config),
              "ProviderCatalogRepository should parse the repository baseline model metadata");
  const auto snapshot = repository.snapshot();

  const auto* chat_model = snapshot->find_model("deepseek-prod", "deepseek-chat");
  const auto* reasoning_model = snapshot->find_model("deepseek-prod", "deepseek-reasoner");
  assert_true(chat_model != nullptr && reasoning_model != nullptr,
              "baseline provider catalog should expose both deepseek baseline models");
  assert_true(chat_model->summary.context_window == 131072U,
              "model catalog should parse context_window for budget guards");
  assert_true(chat_model->summary.default_max_output_tokens == 4096U,
              "model catalog should parse default_max_output_tokens for budget guards");
  assert_true(std::fabs(chat_model->summary.input_cache_hit_usd_per_1m - 0.014) < 0.000001,
              "model catalog should parse pricing metadata for cache-hit cost estimation");
  assert_equal(std::string("https://api-docs.deepseek.com/quick_start/pricing"),
               chat_model->summary.metadata_source_uri,
               "model catalog should preserve metadata source uris");
  assert_equal(std::string("2026-04-10"), chat_model->summary.metadata_effective_at,
               "model catalog should preserve metadata effective dates");
  assert_equal(std::string("limited"), reasoning_model->summary.verification_state,
               "feature-level verification states should aggregate into the routed summary state");
  assert_equal(std::string("needs_integration_validation"),
               reasoning_model->verification_state_for("tools"),
               "feature-level verification_state entries should remain queryable");
  assert_true(std::find(reasoning_model->response_private_fields.begin(),
                        reasoning_model->response_private_fields.end(),
                        "reasoning_content") != reasoning_model->response_private_fields.end(),
              "provider-private response fields should remain in provider metadata instead of shared contracts");
}

}  // namespace

int main() {
  try {
    test_repository_parses_provider_model_metadata_for_budget_and_pricing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
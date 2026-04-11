#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/provider/ProviderCatalogRepository.h"

namespace {

void test_repository_loads_repo_baseline_provider_catalog() {
  using dasall::llm::ProviderCatalogSourceConfig;
  using dasall::llm::provider::ProviderCatalogRepository;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ProviderCatalogRepository repository;
  ProviderCatalogSourceConfig config;
  config.baseline_root =
      (std::filesystem::path(DASALL_REPO_ROOT) / "llm/assets/providers").generic_string();

  assert_true(repository.init(config),
              "ProviderCatalogRepository should load the repository baseline provider assets");
  const auto snapshot = repository.snapshot();
  assert_true(snapshot != nullptr, "ProviderCatalogRepository should publish a catalog snapshot");
  assert_true(snapshot->has_consistent_values(),
              "published provider catalog should satisfy repository invariants");

  const auto* provider = snapshot->find_provider("deepseek-prod");
  assert_true(provider != nullptr,
              "baseline provider catalog should expose the deepseek-prod provider");
  assert_equal(std::string("openai_compatible"), provider->descriptor.adapter_family,
               "provider manifest should map adapter_family into ProviderDescriptor");
  assert_true(provider->descriptor.auth_ref.starts_with("secret://"),
              "provider auth_ref should remain a secret reference instead of a plaintext token");
  assert_true(provider->runtime.overlay_field_is_mutable("auth_ref"),
              "provider manifest should declare auth_ref as a mutable overlay field");
  assert_true(provider->runtime.overlay_field_is_mutable("header_refs"),
              "provider manifest should declare header_refs as a mutable overlay field");
  assert_true(provider->runtime.overlay_field_is_mutable("base_url_alias"),
              "provider manifest should declare base_url_alias as a mutable overlay field");
  assert_equal(std::string("deepseek/default"), provider->runtime.base_url_alias,
               "provider manifest should expose the baseline base_url_alias");
}

}  // namespace

int main() {
  try {
    test_repository_loads_repo_baseline_provider_catalog();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
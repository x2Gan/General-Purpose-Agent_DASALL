#include <exception>
#include <iostream>
#include <string>

#include "LLMSubsystemConfig.h"
#include "config/InstallLayout.h"
#include "support/TestAssertions.h"

namespace {

void test_llm_baseline_sources_follow_install_layout_defaults() {
  using dasall::infra::config::resolve_install_layout;
  using dasall::llm::PromptAssetSourceConfig;
  using dasall::llm::ProviderCatalogSourceConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto install_layout = resolve_install_layout();
  const PromptAssetSourceConfig prompt_sources;
  const ProviderCatalogSourceConfig provider_sources;

  assert_true(install_layout.has_consistent_values(),
              "install-aware layout should expose absolute baseline asset roots for llm defaults");
  assert_equal(install_layout.llm_prompts_root.string(), prompt_sources.baseline_root,
               "PromptAssetSourceConfig should default baseline_root to the install-aware prompt root");
  assert_equal(install_layout.llm_providers_root.string(), provider_sources.baseline_root,
               "ProviderCatalogSourceConfig should default baseline_root to the install-aware provider root");
}

void test_llm_overlay_defaults_remain_consistent_after_install_layout_switch() {
  using dasall::llm::LLMSubsystemConfigOverlay;
  using dasall::tests::support::assert_true;

  const LLMSubsystemConfigOverlay overlay;
  assert_true(overlay.has_consistent_values(),
              "LLMSubsystemConfigOverlay defaults should stay internally consistent after switching baseline roots to the install-aware layout");
}

}  // namespace

int main() {
  try {
    test_llm_baseline_sources_follow_install_layout_defaults();
    test_llm_overlay_defaults_remain_consistent_after_install_layout_switch();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
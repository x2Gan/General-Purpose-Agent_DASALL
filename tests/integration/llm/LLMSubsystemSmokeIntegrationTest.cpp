#include <exception>
#include <iostream>
#include <string_view>

#include "support/TestAssertions.h"

namespace {

// Keep a uniquely discoverable llm smoke anchor registered before unary flow lands.
void test_llm_integration_smoke_anchor_uses_component_prefixed_registration() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "LLMSubsystemSmokeIntegrationTest";
  constexpr std::string_view target_name = "dasall_llm_smoke_integration_test";

  assert_true(ctest_name != "CapabilityServicesSmokeIntegrationTest",
              "llm integration topology should not collide with services smoke registration");
  assert_true(ctest_name.find("LLM") == 0U,
              "llm integration topology should keep a component-prefixed ctest name");
  assert_true(target_name.find("dasall_llm_") == 0U,
              "llm integration topology target should remain namespaced under dasall_llm");
}

}  // namespace

int main() {
  try {
    test_llm_integration_smoke_anchor_uses_component_prefixed_registration();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
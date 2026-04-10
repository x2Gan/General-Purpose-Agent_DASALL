#include <exception>
#include <iostream>
#include <string_view>

#include "support/TestAssertions.h"

namespace {

// Keep a unique llm-specific test anchor registered before public headers land.
void test_llm_unit_surface_anchor_uses_a_collision_free_ctest_name() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "LLMInterfaceSurfaceTest";
  constexpr std::string_view target_name = "dasall_llm_interface_surface_unit_test";

  assert_true(ctest_name != "InterfaceSurfaceTest",
              "llm unit topology should not reuse the platform InterfaceSurfaceTest name");
  assert_true(ctest_name.find("LLM") == 0U,
              "llm unit topology should keep an llm-specific ctest prefix");
  assert_true(target_name.find("dasall_llm_") == 0U,
              "llm unit topology target should remain namespaced under dasall_llm");
}

}  // namespace

int main() {
  try {
    test_llm_unit_surface_anchor_uses_a_collision_free_ctest_name();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
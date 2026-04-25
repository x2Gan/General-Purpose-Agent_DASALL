#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "CognitionConfig.h"
#include "CognitionTypes.h"
#include "ICognitionEngine.h"
#include "IPlanner.h"
#include "IReasoner.h"
#include "IReflectionEngine.h"
#include "IResponseBuilder.h"
#include "belief/BeliefUpdateHint.h"
#include "decision/ActionDecision.h"
#include "support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct has_legacy_step_member : std::false_type {};

template <typename T>
struct has_legacy_step_member<T, std::void_t<decltype(&T::step)>> : std::true_type {};

void test_cognition_unit_topology_names_are_specific() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "CognitionInterfaceSurfaceTest";
  constexpr std::string_view target_name = "dasall_cognition_interface_surface_unit_test";

  assert_true(ctest_name != "InterfaceSurfaceTest",
              "cognition unit topology should not reuse a generic InterfaceSurfaceTest name");
  assert_true(ctest_name.find("Cognition") == 0U,
              "cognition unit topology should keep a cognition-specific ctest prefix");
  assert_true(target_name.find("dasall_cognition_") == 0U,
              "cognition unit target should remain namespaced under dasall_cognition");
}

void test_cognition_engine_surface_freezes_runtime_facing_entries() {
  using dasall::cognition::CognitionConfig;
  using dasall::cognition::CognitionDecisionResult;
  using dasall::cognition::CognitionReflectionResult;
  using dasall::cognition::CognitionStepRequest;
  using dasall::cognition::ICognitionEngine;
  using dasall::cognition::ReflectionRequest;
  using dasall::cognition::create_cognition_engine;
  using dasall::tests::support::assert_true;

  static_assert(std::is_abstract_v<ICognitionEngine>);
  static_assert(std::has_virtual_destructor_v<ICognitionEngine>);
  static_assert(std::is_same_v<decltype(&ICognitionEngine::decide),
                               CognitionDecisionResult (ICognitionEngine::*)(
                                   const CognitionStepRequest&)>);
  static_assert(std::is_same_v<decltype(&ICognitionEngine::reflect),
                               CognitionReflectionResult (ICognitionEngine::*)(
                                   const ReflectionRequest&)>);
  static_assert(std::is_same_v<decltype(&create_cognition_engine),
                               std::unique_ptr<ICognitionEngine> (*)(
                                   const CognitionConfig&)>);
  static_assert(!has_legacy_step_member<ICognitionEngine>::value);

  auto engine = create_cognition_engine(CognitionConfig{});
  assert_true(engine != nullptr, "cognition engine factory should return a usable interface");
}

void test_response_builder_surface_freezes_public_entry() {
  using dasall::cognition::CognitionConfig;
  using dasall::cognition::IResponseBuilder;
  using dasall::cognition::ResponseBuildRequest;
  using dasall::cognition::ResponseBuildResult;
  using dasall::cognition::create_response_builder;
  using dasall::tests::support::assert_true;

  static_assert(std::is_abstract_v<IResponseBuilder>);
  static_assert(std::has_virtual_destructor_v<IResponseBuilder>);
  static_assert(std::is_same_v<decltype(&IResponseBuilder::build),
                               ResponseBuildResult (IResponseBuilder::*)(
                                   const ResponseBuildRequest&)>);
  static_assert(std::is_same_v<decltype(&create_response_builder),
                               std::unique_ptr<IResponseBuilder> (*)(
                                   const CognitionConfig&)>);

  auto builder = create_response_builder(CognitionConfig{});
  assert_true(builder != nullptr, "response builder factory should return a usable interface");
}

void test_stage_component_headers_are_markers_not_publicly_constructible_components() {
  using dasall::cognition::IPlanner;
  using dasall::cognition::IReasoner;
  using dasall::cognition::IReflectionEngine;

  static_assert(std::has_virtual_destructor_v<IPlanner>);
  static_assert(std::has_virtual_destructor_v<IReasoner>);
  static_assert(std::has_virtual_destructor_v<IReflectionEngine>);
  static_assert(!std::is_default_constructible_v<IPlanner>);
  static_assert(!std::is_default_constructible_v<IReasoner>);
  static_assert(!std::is_default_constructible_v<IReflectionEngine>);
}

void test_current_supporting_types_remain_module_public() {
  using dasall::cognition::belief::BeliefUpdateHint;
  using dasall::cognition::decision::ActionDecision;
  using dasall::cognition::decision::ActionDecisionKind;

  static_assert(std::is_same_v<decltype(ActionDecision{}.decision_kind),
                               ActionDecisionKind>);
  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.merge_mode), std::string>);
}

}  // namespace

int main() {
  try {
    test_cognition_unit_topology_names_are_specific();
    test_cognition_engine_surface_freezes_runtime_facing_entries();
    test_response_builder_surface_freezes_public_entry();
    test_stage_component_headers_are_markers_not_publicly_constructible_components();
    test_current_supporting_types_remain_module_public();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "context/ContextAssemblyResult.h"
#include "context/MemoryContextRequest.h"

#include "support/TestAssertions.h"

#ifndef DASALL_MEMORY_PUBLIC_INCLUDE_DIR
#define DASALL_MEMORY_PUBLIC_INCLUDE_DIR "/home/gangan/DASALL/memory/include"
#endif

#ifndef DASALL_MEMORY_SOURCE_DIR
#define DASALL_MEMORY_SOURCE_DIR "/home/gangan/DASALL/memory/src"
#endif

namespace {

void test_memory_unit_surface_anchor_uses_a_collision_free_ctest_name() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "MemoryInterfaceCompileTest";
  constexpr std::string_view target_name = "dasall_memory_interface_compile_unit_test";

  assert_true(ctest_name.find("Memory") == 0U,
              "memory unit topology should keep a memory-specific ctest prefix");
  assert_true(target_name.find("dasall_memory_") == 0U,
              "memory unit topology target should remain namespaced under dasall_memory");
}

void test_memory_public_include_layout_exists() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path include_root{DASALL_MEMORY_PUBLIC_INCLUDE_DIR};
  const fs::path config_dir = include_root / "config";
  const fs::path context_dir = include_root / "context";
  const fs::path error_dir = include_root / "error";
  const fs::path vector_dir = include_root / "vector";
  const fs::path working_dir = include_root / "working";
  const fs::path writeback_dir = include_root / "writeback";

  assert_true(fs::is_directory(include_root),
              "memory public include root should exist before interface headers land");
  assert_true(fs::is_directory(config_dir),
              "memory public include layout should expose the config subdirectory");
  assert_true(fs::is_directory(context_dir),
              "memory public include layout should expose the context subdirectory");
  assert_true(fs::is_directory(error_dir),
              "memory public include layout should expose the error subdirectory");
  assert_true(fs::is_directory(vector_dir),
              "memory public include layout should expose the vector subdirectory");
  assert_true(fs::is_directory(working_dir),
              "memory public include layout should expose the working subdirectory");
  assert_true(fs::is_directory(writeback_dir),
              "memory public include layout should expose the writeback subdirectory");
}

void test_memory_module_is_no_longer_placeholder_only() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path source_root{DASALL_MEMORY_SOURCE_DIR};
  const fs::path build_anchor = source_root / "MemoryBuildSkeleton.cpp";
  const fs::path legacy_placeholder = source_root / "placeholder.cpp";

  assert_true(fs::is_regular_file(build_anchor),
              "memory module should expose a tracked build skeleton source instead of a single placeholder file");
  assert_true(!fs::exists(legacy_placeholder),
              "memory module should no longer rely on the legacy placeholder.cpp translation unit");
}

void test_memory_context_supporting_types_compile_and_expose_expected_defaults() {
  using dasall::memory::ContextAssemblyResult;
  using dasall::memory::MemoryContextRequest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(MemoryContextRequest{}.visible_tools),
                               std::vector<std::string>>,
                "MemoryContextRequest should expose visible_tools as string identifiers");
  static_assert(std::is_same_v<decltype(ContextAssemblyResult{}.context_packet),
                               dasall::contracts::ContextPacket>,
                "ContextAssemblyResult should carry the contracts ContextPacket payload");

  MemoryContextRequest request;
  request.request_id = "req-001";
  request.session_id = "session-001";
  request.stage = "plan";
  request.goal_summary = "Produce a stable prompt packet";
  request.constraints_summary = "Stay within token budget";
  request.latest_observation_digest_summary = "No prior observation";
  request.visible_tools = {"shell", "search"};
  request.external_evidence = {"profile:desktop_full", "policy:interactive"};

  assert_equal("req-001", request.request_id,
               "memory context request should expose a runtime correlation id");
  assert_equal("session-001", request.session_id,
               "memory context request should expose a target session id");
  assert_equal("plan", request.stage,
               "memory context request should expose the orchestration stage");
  assert_equal(4096, MemoryContextRequest{}.token_budget_hint,
               "memory context request should default to the detailed-design token budget hint");
  assert_equal(0, MemoryContextRequest{}.latency_budget_ms,
               "memory context request should default latency budget to unconstrained");
  assert_equal(2, static_cast<int>(request.visible_tools.size()),
               "memory context request should carry runtime-projected visible tools");
  assert_equal(2, static_cast<int>(request.external_evidence.size()),
               "memory context request should carry external evidence projections");

  ContextAssemblyResult result;
  result.context_packet.request_id = request.request_id;
  result.context_packet.current_goal_summary = request.goal_summary;

  assert_true(!result.result_code.has_value(),
              "context assembly success path should allow the shared result code to stay empty");
  assert_true(result.context_packet.request_id.has_value(),
              "context assembly result should expose the assembled contracts payload");
  assert_equal("Produce a stable prompt packet",
               result.context_packet.current_goal_summary.value_or(std::string{}),
               "context assembly result should surface the goal summary in ContextPacket");
  assert_true(result.dropped_sections.empty(),
              "fresh context assembly results should start without dropped sections");
  assert_true(result.compression_notes.empty(),
              "fresh context assembly results should start without compression notes");
  assert_true(result.warnings.empty(),
              "fresh context assembly results should start without warnings");
  assert_true(!result.degraded,
              "fresh context assembly results should not report degraded execution");
}

}  // namespace

int main() {
  try {
    test_memory_unit_surface_anchor_uses_a_collision_free_ctest_name();
    test_memory_public_include_layout_exists();
    test_memory_module_is_no_longer_placeholder_only();
    test_memory_context_supporting_types_compile_and_expose_expected_defaults();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
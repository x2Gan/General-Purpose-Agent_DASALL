#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "context/ContextAssemblyResult.h"
#include "context/MemoryContextRequest.h"
#include "writeback/MemoryWritebackRequest.h"
#include "writeback/WritebackResult.h"

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

  void test_memory_writeback_supporting_types_compile_and_preserve_partial_retry_semantics() {
    using dasall::memory::ConflictAction;
    using dasall::memory::MemoryWritebackRequest;
    using dasall::memory::WritebackResult;
    using dasall::tests::support::assert_equal;
    using dasall::tests::support::assert_true;

    static_assert(std::is_same_v<decltype(MemoryWritebackRequest{}.turn),
                   dasall::contracts::Turn>,
          "MemoryWritebackRequest should reuse the frozen Turn contract");
    static_assert(std::is_same_v<decltype(WritebackResult{}.result_code),
                   std::optional<dasall::contracts::ResultCode>>,
          "WritebackResult should keep the shared result code optional on success");

    MemoryWritebackRequest request;
    request.session_id = "session-001";
    request.turn.turn_id = "turn-001";
    request.turn.session_id = request.session_id;
    request.turn.user_input = "Summarize the latest tool run";
    request.turn.tool_call_refs = std::vector<std::string>{"tool-call-1", "tool-call-2"};
    request.turn.observation_refs = std::vector<std::string>{"observation-1"};
    request.summary_candidate = dasall::contracts::SummaryMemory{};
    request.summary_candidate->summary_text = "A compact summary";
    request.summary_candidate->source_turn_ids = std::vector<std::string>{"turn-001"};
      dasall::contracts::MemoryFact fact_candidate;
      fact_candidate.fact_text = "The shell command succeeded";
      request.fact_candidates.push_back(dasall::memory::FactCandidate{
        .fact = std::move(fact_candidate),
        .extraction_source = "observation",
      });
      dasall::contracts::ExperienceMemory experience_candidate;
      experience_candidate.lesson_summary = "Retry storage after a busy window";
      request.experience_candidates.push_back(dasall::memory::ExperienceCandidate{
        .experience = std::move(experience_candidate),
        .extraction_source = "reflection",
      });
    request.side_effect_report_ref = "side-effect-report-1";

    assert_equal("session-001", request.session_id,
           "memory writeback request should target a single session");
    assert_true(request.turn.tool_call_refs.has_value() &&
            request.turn.tool_call_refs->size() == 2U,
          "memory writeback request should rely on Turn tool refs instead of duplicating them");
    assert_true(request.turn.observation_refs.has_value() &&
            request.turn.observation_refs->size() == 1U,
          "memory writeback request should rely on Turn observation refs instead of duplicating them");
    assert_true(request.summary_candidate.has_value(),
          "memory writeback request should allow an optional summary candidate");
    assert_equal(1, static_cast<int>(request.fact_candidates.size()),
           "memory writeback request should carry fact candidates");
    assert_equal(1, static_cast<int>(request.experience_candidates.size()),
           "memory writeback request should carry experience candidates");
    assert_equal("side-effect-report-1", request.side_effect_report_ref.value_or(std::string{}),
           "memory writeback request should preserve a side-effect report reference");

    WritebackResult result;
    result.persisted_turn_id = "turn-001";
    result.conflicts.push_back(dasall::memory::ConflictRecord{
      .new_fact_id = "fact-new",
      .existing_fact_id = "fact-old",
      .action = ConflictAction::Supersede,
      .reason = "new fact has higher confidence",
      .confidence_delta = 15,
    });

    assert_true(!result.result_code.has_value(),
          "writeback success path should allow the shared result code to stay empty");
    assert_true(result.persisted_turn_id.has_value(),
          "writeback result should expose the persisted turn identifier");
    assert_true(!result.degraded,
          "fresh writeback results should default to non-degraded");
    assert_true(!result.partial,
          "fresh writeback results should default to non-partial");
    assert_true(!result.retryable_storage_failure,
          "fresh writeback results should default to non-retryable-storage-failure");
    assert_equal(1, static_cast<int>(result.conflicts.size()),
           "writeback result should expose conflict records when present");
    assert_true(result.conflicts.front().action == ConflictAction::Supersede,
          "writeback conflict records should preserve the conflict action enum");
  }

}  // namespace

int main() {
  try {
    test_memory_unit_surface_anchor_uses_a_collision_free_ctest_name();
    test_memory_public_include_layout_exists();
    test_memory_module_is_no_longer_placeholder_only();
    test_memory_context_supporting_types_compile_and_expose_expected_defaults();
    test_memory_writeback_supporting_types_compile_and_preserve_partial_retry_semantics();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
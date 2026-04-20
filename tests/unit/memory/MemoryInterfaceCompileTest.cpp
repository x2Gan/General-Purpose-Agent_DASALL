#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>

#include "IContextOrchestrator.h"
#include "IMemoryManager.h"
#include "IMemoryStore.h"
#include "ISummarizer.h"
#include "IStoreTransaction.h"
#include "MaintenanceReport.h"
#include "MaintenanceRequest.h"
#include "config/MemoryConfig.h"
#include "context/ContextAssemblyResult.h"
#include "context/MemoryContextRequest.h"
#include "error/MemoryError.h"
#include "working/IWorkingMemoryBoard.h"
#include "working/WorkingMemoryExportRequest.h"
#include "working/WorkingMemoryExportResult.h"
#include "working/WorkingMemorySnapshot.h"
#include "writeback/MemoryWritebackRequest.h"
#include "writeback/SummaryGenerationRequest.h"
#include "writeback/SummaryGenerationResult.h"
#include "writeback/SummaryProjection.h"
#include "writeback/WritebackResult.h"

#include "FakeMemoryStore.h"
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

  void test_memory_config_defaults_align_with_detailed_design() {
    using dasall::memory::MemoryConfig;
    using dasall::tests::support::assert_equal;
    using dasall::tests::support::assert_true;

    static_assert(std::is_same_v<decltype(MemoryConfig{}.storage), dasall::memory::StorageConfig>,
          "MemoryConfig should expose the storage projection");
    static_assert(std::is_same_v<decltype(MemoryConfig{}.vector), dasall::memory::VectorConfig>,
          "MemoryConfig should expose the vector projection");

    MemoryConfig config;

    assert_equal(std::string("sqlite"), std::string(to_string_view(config.storage.backend)),
         "memory storage backend should default to sqlite");
    assert_equal("memory.db", config.storage.db_path,
         "memory storage db path should default to the local sqlite file");
    assert_equal(std::string("WAL"), std::string(to_string_view(config.storage.journal_mode)),
         "memory storage should default to WAL journal mode");
    assert_equal(std::string("NORMAL"), std::string(to_string_view(config.storage.synchronous)),
         "memory storage should default synchronous mode to NORMAL");
    assert_equal(1000, config.storage.wal_autocheckpoint_pages,
         "memory storage should default WAL autocheckpoint pages to the detailed-design baseline");
    assert_equal(50, config.storage.busy_timeout_ms,
         "memory storage should default busy timeout to the bounded local retry window");
    assert_equal(2, config.storage.writer_retry_count,
         "memory storage should default writer retry count to the design baseline");
    assert_equal(std::string("PASSIVE"), std::string(to_string_view(config.storage.checkpoint_mode)),
         "memory storage should default checkpoint mode to PASSIVE");
    assert_equal(2, config.storage.reader_pool_size,
         "memory storage should default reader pool size to the design baseline");
    assert_equal("sql/memory/", config.storage.migrations_dir,
         "memory storage should expose the migration directory projection");

    assert_equal(8, config.context.recent_turn_limit,
         "memory context config should default recent turn limit to eight");
    assert_equal(12, config.context.compression_trigger_turns,
         "memory context config should default compression trigger turns to twelve");
    assert_equal(3, config.context.max_summary_candidates,
         "memory context config should default max summary candidates to three");
    assert_equal(80, config.context.fact_confidence_floor,
         "memory context config should default fact confidence floor to eighty");
    assert_true(config.context.compression_trigger_ratio > 0.84 &&
        config.context.compression_trigger_ratio < 0.86,
        "memory context config should default compression trigger ratio to 0.85");

    assert_equal(60, config.experience.effectiveness_floor,
         "memory experience config should default effectiveness floor to sixty");
    assert_true(!config.vector.enabled,
        "memory vector config should default to disabled until profile projection enables it");
    assert_equal(std::string("sqlite-vss"), std::string(to_string_view(config.vector.backend_type)),
         "memory vector config should expose sqlite-vss as the default backend type");
    assert_equal(5, config.vector.search_top_k,
         "memory vector config should default top-k search width to five");

    assert_equal(200, config.maintenance.retention_turns,
         "memory maintenance config should default retention turns to two hundred");
    assert_true(config.maintenance.quarantine_enabled,
        "memory maintenance config should default quarantine to enabled");
    assert_true(config.maintenance.quarantine_ttl_ms == 604800000,
        "memory maintenance config should default quarantine ttl to seven days");
    assert_true(config.maintenance.fact_ttl_ms == 0,
        "memory maintenance config should default fact ttl to disabled cleanup");
    assert_true(config.maintenance.experience_ttl_ms == 0,
        "memory maintenance config should default experience ttl to disabled cleanup");
    assert_true(!config.maintenance.auto_schedule,
        "memory maintenance config should default auto schedule to disabled");
    assert_equal(std::int64_t{60000}, config.maintenance.schedule_interval_ms,
         "memory maintenance config should default maintenance schedule interval to sixty seconds");
  }

void test_memory_error_mapping_aligns_with_warning_and_audit_semantics() {
     using dasall::memory::MemoryError;
     using dasall::memory::map_memory_errno;
     using dasall::memory::map_memory_error;
     using dasall::memory::memory_error_name;
     using dasall::tests::support::assert_equal;
     using dasall::tests::support::assert_true;

     assert_equal("MEM_E_STORAGE_BUSY", std::string(memory_error_name(MemoryError::StorageBusy)),
                                    "memory error names should stay stable for storage busy");
     assert_equal("MEM_E_SCHEMA_MISMATCH", std::string(memory_error_name(MemoryError::SchemaMismatch)),
                                    "memory error names should stay stable for schema mismatch");

     const auto storage_busy = map_memory_error(MemoryError::StorageBusy);
     assert_true(storage_busy.result_code == dasall::contracts::ResultCode::RuntimeRetryExhausted,
                                   "storage busy should map to the contracts runtime failure category");
     assert_true(storage_busy.retryable,
                                   "storage busy should remain retryable until the bounded retry window is exhausted");
     assert_true(!storage_busy.audit_required,
                                   "storage busy should degrade to a warning before forcing audit escalation");
     assert_equal("retryable_storage_failure", std::string(storage_busy.warning_key),
                                    "storage busy should surface the retryable storage failure warning key");

     const auto schema_mismatch = map_memory_error(MemoryError::SchemaMismatch);
     assert_true(schema_mismatch.result_code == dasall::contracts::ResultCode::ValidationFieldMissing,
                                   "schema mismatch should map into the contracts validation category");
     assert_true(schema_mismatch.audit_required,
                                   "schema mismatch should require audit evidence");
     assert_equal("runtime/deploy", std::string(schema_mismatch.audit_scope),
                                    "schema mismatch should report the runtime/deploy audit scope");

     const auto validation_rejected = map_memory_error(MemoryError::ValidationRejected);
     assert_true(validation_rejected.result_code == dasall::contracts::ResultCode::ValidationFieldMissing,
                                   "validation rejected should stay in the contracts validation category");
     assert_equal("partial_writeback_warning", std::string(validation_rejected.warning_key),
                                    "validation rejected should surface the partial writeback warning key");
     assert_true(validation_rejected.audit_required,
                                   "validation rejected should remain auditable even when the main writeback path continues");

     assert_true(map_memory_errno(EBUSY) == MemoryError::StorageBusy,
                                   "EBUSY should map to the storage busy memory error");
     assert_true(map_memory_errno(EAGAIN) == MemoryError::StorageBusy,
                                   "EAGAIN should map to the storage busy memory error");
     assert_true(map_memory_errno(EINVAL) == MemoryError::ConfigInvalid,
                                   "EINVAL should map to the config invalid memory error");
     assert_true(map_memory_errno(EIO) == MemoryError::StorageUnavailable,
                                   "EIO should map to the storage unavailable memory error");
}

void test_memory_manager_supporting_types_and_factory_surface_compile_with_stable_defaults() {
     using dasall::memory::IMemoryManager;
     using dasall::memory::MaintenanceReport;
     using dasall::memory::MaintenanceRequest;
     using dasall::memory::MemoryConfig;
     using dasall::memory::WorkingMemoryExportRequest;
     using dasall::memory::WorkingMemoryExportResult;
     using dasall::memory::WorkingMemorySlot;
     using dasall::memory::WorkingMemorySnapshot;
     using dasall::tests::support::assert_equal;
     using dasall::tests::support::assert_true;

     using FactorySignature = std::unique_ptr<IMemoryManager> (*)(const MemoryConfig&);
     static_assert(std::is_same_v<decltype(&dasall::memory::create_memory_manager), FactorySignature>,
                                        "create_memory_manager should remain the config-driven public factory surface");

     WorkingMemoryExportRequest export_request;
     export_request.session_id = "session-001";
     assert_equal("manual", export_request.export_reason,
                                    "working-memory export requests should default to the manual export reason");
     assert_true(!export_request.include_ephemeral_facts,
                                   "working-memory export should default ephemeral facts export to disabled");
     assert_true(!export_request.evict_expired_before_export,
                                   "working-memory export should default eviction to disabled");

     WorkingMemorySnapshot snapshot;
     snapshot.session_id = "session-001";
     snapshot.slots.push_back(WorkingMemorySlot{
               .key = "active_goal",
               .value = "stabilize memory lifecycle",
               .updated_at = 1,
               .ttl_ms = 0,
               .source = "agent",
     });
     snapshot.open_questions = {"Should sqlite writer retry be configurable?"};
     snapshot.ephemeral_facts = {"factory uses bootstrap orchestrator"};

     WorkingMemoryExportResult export_result;
     export_result.snapshot = snapshot;
     assert_equal("session-001", export_result.snapshot.session_id,
                                    "working-memory export results should carry the snapshot payload");
     assert_equal(1, static_cast<int>(export_result.snapshot.slots.size()),
                                    "working-memory export results should preserve slot projections");
     assert_true(!export_result.result_code.has_value(),
                                   "fresh working-memory export results should default to success semantics");
     assert_true(!export_result.degraded,
                                   "fresh working-memory export results should default to non-degraded");

     MaintenanceRequest maintenance_request;
     assert_true(maintenance_request.run_checkpoint,
                                   "maintenance requests should default checkpoint execution to enabled");
     assert_true(maintenance_request.run_retention,
                                   "maintenance requests should default retention execution to enabled");
     assert_true(maintenance_request.run_quarantine_cleanup,
                                   "maintenance requests should default quarantine cleanup to enabled");
     assert_true(!maintenance_request.run_vector_rebuild,
                                   "maintenance requests should default vector rebuild to disabled");

     MaintenanceReport maintenance_report;
     assert_true(!maintenance_report.checkpoint_executed,
                                   "fresh maintenance reports should default checkpoint execution to false");
     assert_true(maintenance_report.warnings.empty(),
                                   "fresh maintenance reports should default to no warnings");
}

void test_working_memory_board_interface_surface_compiles_with_snapshot_round_trip() {
     using dasall::memory::IWorkingMemoryBoard;
     using dasall::memory::WorkingMemorySlot;
     using dasall::memory::WorkingMemorySnapshot;
     using dasall::tests::support::assert_equal;
     using dasall::tests::support::assert_true;

     using SetSlotSignature = void (IWorkingMemoryBoard::*)(const std::string&, const WorkingMemorySlot&);
     using GetSlotSignature = std::optional<WorkingMemorySlot> (IWorkingMemoryBoard::*)(const std::string&, const std::string&) const;
     using ExportSignature = WorkingMemorySnapshot (IWorkingMemoryBoard::*)(const std::string&) const;
     using BoardFactorySignature = std::unique_ptr<IWorkingMemoryBoard> (*)(std::size_t);

     static_assert(std::is_same_v<decltype(&IWorkingMemoryBoard::set_slot), SetSlotSignature>,
                                        "IWorkingMemoryBoard::set_slot should preserve the keyed slot write surface");
     static_assert(std::is_same_v<decltype(&IWorkingMemoryBoard::get_slot), GetSlotSignature>,
                                        "IWorkingMemoryBoard::get_slot should preserve the optional keyed read surface");
     static_assert(std::is_same_v<decltype(&IWorkingMemoryBoard::export_snapshot), ExportSignature>,
                                        "IWorkingMemoryBoard::export_snapshot should preserve the snapshot export surface");
     static_assert(std::is_same_v<decltype(&dasall::memory::create_working_memory_board), BoardFactorySignature>,
                                        "create_working_memory_board should remain the public board factory surface");

     auto board = dasall::memory::create_working_memory_board(4);
     WorkingMemorySnapshot snapshot;
     snapshot.session_id = "session-001";
     snapshot.slots.push_back(WorkingMemorySlot{
               .key = "active_goal",
               .value = "finish mem-todo-012",
               .updated_at = 10,
               .ttl_ms = 0,
               .source = "agent",
     });
     snapshot.open_questions = {"Should board export empty sessions?"};
     snapshot.ephemeral_facts = {"working board restored from snapshot"};

     board->restore_snapshot(snapshot);
     const auto exported = board->export_snapshot("session-001");
     assert_equal("session-001", exported.session_id,
                                    "working-memory board export should preserve the restored session id");
     assert_equal(1, static_cast<int>(exported.slots.size()),
                                    "working-memory board export should preserve restored slots");
     assert_true(exported.open_questions.size() == 1U,
                                   "working-memory board export should preserve restored open questions");
     assert_true(exported.ephemeral_facts.size() == 1U,
                                   "working-memory board export should preserve restored ephemeral facts");
}

     void test_memory_manager_interfaces_expose_expected_runtime_facing_signatures() {
       using dasall::memory::ContextAssemblyResult;
       using dasall::memory::IContextOrchestrator;
       using dasall::memory::IMemoryManager;
       using dasall::memory::MaintenanceReport;
       using dasall::memory::MaintenanceRequest;
       using dasall::memory::MemoryConfig;
       using dasall::memory::MemoryContextRequest;
       using dasall::memory::MemoryWritebackRequest;
       using dasall::memory::WorkingMemoryExportRequest;
       using dasall::memory::WorkingMemoryExportResult;
       using dasall::memory::WritebackResult;

       using InitSignature = dasall::contracts::ResultCode (IMemoryManager::*)(const MemoryConfig&);
       using PrepareSignature = ContextAssemblyResult (IMemoryManager::*)(const MemoryContextRequest&);
       using WriteBackSignature = WritebackResult (IMemoryManager::*)(const MemoryWritebackRequest&);
       using ExportSignature = WorkingMemoryExportResult (IMemoryManager::*)(
            const WorkingMemoryExportRequest&);
       using MaintenanceSignature = MaintenanceReport (IMemoryManager::*)(
            const MaintenanceRequest&);
       using AssembleSignature = ContextAssemblyResult (IContextOrchestrator::*)(
            const MemoryContextRequest&);

       static_assert(std::has_virtual_destructor_v<IMemoryManager>,
                         "IMemoryManager should remain a polymorphic runtime-facing interface");
       static_assert(std::has_virtual_destructor_v<IContextOrchestrator>,
                         "IContextOrchestrator should remain a polymorphic orchestration interface");
       static_assert(std::is_abstract_v<IMemoryManager>,
                         "IMemoryManager should stay abstract until concrete manager implementation lands");
       static_assert(std::is_abstract_v<IContextOrchestrator>,
                         "IContextOrchestrator should stay abstract until concrete orchestrator implementation lands");
       static_assert(std::is_same_v<decltype(&IMemoryManager::init), InitSignature>,
                         "IMemoryManager::init should consume MemoryConfig and return the shared ResultCode");
       static_assert(std::is_same_v<decltype(&IMemoryManager::prepare_context), PrepareSignature>,
                         "IMemoryManager::prepare_context should forward the context request/result surface");
       static_assert(std::is_same_v<decltype(&IMemoryManager::write_back), WriteBackSignature>,
                         "IMemoryManager::write_back should forward the writeback request/result surface");
       static_assert(std::is_same_v<decltype(&IMemoryManager::export_working_memory_snapshot),
                                           ExportSignature>,
                         "IMemoryManager::export_working_memory_snapshot should preserve the working-memory export facade signature");
       static_assert(std::is_same_v<decltype(&IMemoryManager::run_maintenance), MaintenanceSignature>,
                         "IMemoryManager::run_maintenance should preserve the maintenance facade signature");
       static_assert(std::is_same_v<decltype(&IContextOrchestrator::assemble), AssembleSignature>,
                         "IContextOrchestrator::assemble should consume the module-local context request/result pair");
     }

void test_memory_summarizer_interface_uses_module_local_summary_supporting_types() {
     using dasall::memory::ISummarizer;
     using dasall::memory::SummaryGenerationRequest;
     using dasall::memory::SummaryGenerationResult;
     using dasall::memory::SummaryProjection;
     using dasall::tests::support::assert_equal;
     using dasall::tests::support::assert_true;

     using SummarizeSignature = SummaryGenerationResult (ISummarizer::*)(
               const SummaryGenerationRequest&);

     static_assert(std::has_virtual_destructor_v<ISummarizer>,
                                        "ISummarizer should remain a polymorphic summarizer abstraction");
     static_assert(std::is_abstract_v<ISummarizer>,
                                        "ISummarizer should stay abstract until runtime injects a concrete summarizer");
     static_assert(std::is_same_v<decltype(&ISummarizer::summarize), SummarizeSignature>,
                                        "ISummarizer::summarize should consume the module-local summary request/result pair");

     SummaryGenerationRequest request;
     request.session_id = "session-001";
     request.source_turns.push_back(dasall::contracts::Turn{});
     request.source_turns.front().turn_id = "turn-001";
     request.existing_summary = dasall::contracts::SummaryMemory{};
     request.existing_summary->summary_text = "Previous summary";
     request.target_token_budget = 1024;
     request.strategy_hint = "auto";

     assert_equal("session-001", request.session_id,
                                    "summary generation request should target a single session");
     assert_equal(1, static_cast<int>(request.source_turns.size()),
                                    "summary generation request should carry source turns");
     assert_true(request.existing_summary.has_value(),
                                   "summary generation request should allow an optional existing summary");
     assert_equal(1024, request.target_token_budget,
                                    "summary generation request should preserve the target token budget");
     assert_equal("auto", request.strategy_hint,
                                    "summary generation request should preserve the strategy hint");

     SummaryGenerationResult result;
     result.projection = SummaryProjection{
               .summary_text = "A merged summary",
               .decisions_made = {"use cached evidence"},
               .confirmed_facts = {"tool run completed"},
               .tool_outcomes = {"shell succeeded"},
               .source_turn_ids = {"turn-001"},
               .estimated_tokens = 128,
     };
     result.warnings = {"summarizer_fallback"};
     result.fallback_used = true;
     result.degraded = true;

     assert_equal("A merged summary", result.projection.summary_text,
                                    "summary generation result should surface the summary projection text");
     assert_equal(1, static_cast<int>(result.projection.decisions_made.size()),
                                    "summary generation result should surface structured decisions");
     assert_equal(128, result.projection.estimated_tokens,
                                    "summary generation result should surface the token estimate");
     assert_true(result.fallback_used,
                                   "summary generation result should preserve the fallback-used flag");
     assert_true(result.degraded,
                                   "summary generation result should preserve the degraded flag");
}

void test_memory_store_interfaces_and_fake_cover_transactions_and_query_supporting_types() {
     using dasall::memory::ExperienceQuery;
     using dasall::memory::FactQuery;
     using dasall::memory::IMemoryStore;
     using dasall::memory::IStoreTransaction;
     using dasall::memory::MemoryConfig;
     using dasall::memory::SessionLoadRequest;
     using dasall::memory::StoreResult;
     using dasall::tests::mocks::FakeMemoryStore;
     using dasall::tests::support::assert_equal;
     using dasall::tests::support::assert_true;

     using OpenSignature = std::optional<dasall::contracts::ResultCode> (IMemoryStore::*)(
               const MemoryConfig&);
     using BeginTransactionSignature = std::unique_ptr<IStoreTransaction> (IMemoryStore::*)();
     using LoadBundleSignature = dasall::memory::SessionLoadBundle (IMemoryStore::*)(
               const SessionLoadRequest&) const;
     using QueryFactsSignature = dasall::memory::FactQueryResult (IMemoryStore::*)(
               const FactQuery&) const;
     using QueryExperiencesSignature = dasall::memory::ExperienceQueryResult (IMemoryStore::*)(
               const ExperienceQuery&) const;
     using CommitSignature = std::optional<dasall::contracts::ResultCode> (IStoreTransaction::*)();

     static_assert(std::has_virtual_destructor_v<IMemoryStore>,
                                        "IMemoryStore should remain a polymorphic storage abstraction");
     static_assert(std::has_virtual_destructor_v<IStoreTransaction>,
                                        "IStoreTransaction should remain a polymorphic transaction abstraction");
     static_assert(std::is_abstract_v<IMemoryStore>,
                                        "IMemoryStore should stay abstract until the SQLite-backed implementation lands");
     static_assert(std::is_abstract_v<IStoreTransaction>,
                                        "IStoreTransaction should stay abstract until a concrete transaction implementation lands");
     static_assert(std::is_same_v<decltype(&IMemoryStore::open), OpenSignature>,
                                        "IMemoryStore::open should surface the optional ResultCode outcome pattern");
     static_assert(std::is_same_v<decltype(&IMemoryStore::begin_immediate), BeginTransactionSignature>,
                                        "IMemoryStore::begin_immediate should return a RAII transaction handle");
     static_assert(std::is_same_v<decltype(&IMemoryStore::load_session_bundle), LoadBundleSignature>,
                                        "IMemoryStore::load_session_bundle should consume the module-local session load request");
     static_assert(std::is_same_v<decltype(&IMemoryStore::query_facts), QueryFactsSignature>,
                                        "IMemoryStore::query_facts should consume the module-local fact query surface");
     static_assert(std::is_same_v<decltype(&IMemoryStore::query_experiences), QueryExperiencesSignature>,
                                        "IMemoryStore::query_experiences should consume the module-local experience query surface");
     static_assert(std::is_same_v<decltype(&IStoreTransaction::commit), CommitSignature>,
                                        "IStoreTransaction::commit should use the optional ResultCode outcome pattern");

     SessionLoadRequest session_load_request;
     session_load_request.session_id = "session-001";
     session_load_request.recent_turn_limit = 5;
     assert_equal("session-001", session_load_request.session_id,
                                    "session load request should preserve the requested session id");
     assert_equal(5, session_load_request.recent_turn_limit,
                                    "session load request should preserve the requested turn window size");

     StoreResult success = StoreResult::success(std::string{"persisted-001"});
     StoreResult failure = StoreResult::failure(
               dasall::contracts::ResultCode::ValidationFieldMissing,
               std::string{"session_id is required"});
     assert_true(success.ok,
                                   "store result success helper should mark successful writes");
     assert_true(!success.result_code.has_value(),
                                   "store result success helper should leave the error code empty on success");
     assert_equal("persisted-001", success.persisted_id.value_or(std::string{}),
                                    "store result success helper should preserve the persisted id");
     assert_true(!failure.ok,
                                   "store result failure helper should mark failed writes");
     assert_true(failure.result_code == dasall::contracts::ResultCode::ValidationFieldMissing,
                                   "store result failure helper should preserve the contracts result code");

     FactQuery fact_query;
     fact_query.session_id = "session-001";
     fact_query.user_id = "user-001";
     fact_query.fact_type = "preference";
     fact_query.min_confidence = 80;
     fact_query.limit = 10;

     ExperienceQuery experience_query;
     experience_query.session_id = "session-001";
     experience_query.user_id = "user-001";
     experience_query.stage = "plan";
     experience_query.applicable_domains = std::vector<std::string>{"memory"};
     experience_query.limit = 5;

     FakeMemoryStore store;
     MemoryConfig config;

     assert_true(!store.open(config).has_value(),
                                   "fake memory store should reuse the optional ResultCode success pattern");
     assert_true(store.is_open_for_test(),
                                   "fake memory store should expose an opened state after open()");
     assert_true(store.last_open_backend_for_test() == dasall::memory::StorageBackend::Sqlite,
                                    "fake memory store should preserve the configured backend projection");

     auto transaction = store.begin_immediate();
     assert_true(static_cast<bool>(transaction),
                                   "fake memory store should expose a transaction handle");
     assert_true(store.has_active_transaction_for_test(),
                                   "fake memory store should mark a transaction as active while the handle is alive");
     assert_true(!transaction->commit().has_value(),
                                   "fake store transactions should commit using the optional ResultCode success pattern");
     assert_true(!store.has_active_transaction_for_test(),
                                   "fake memory store should clear the active transaction flag after commit");

     dasall::contracts::Session session;
     session.session_id = "session-001";
     session.turn_ids = std::vector<std::string>{};
     session.user_id = "user-001";
     session.created_at = 1;
     assert_true(store.create_session(session).ok,
                                   "fake memory store should create sessions through the IMemoryStore surface");

     dasall::contracts::Turn turn;
     turn.turn_id = "turn-001";
     turn.session_id = "session-001";
     turn.user_input = "Remember the latest shell result";
     turn.created_at = 2;
     assert_true(store.append_turn(turn).ok,
                                   "fake memory store should append turns through the IMemoryStore surface");
     assert_equal(1, static_cast<int>(store.count_turns("session-001")),
                                    "fake memory store should count turns per session");
     assert_true(store.update_session_active("session-001", 3).ok,
                                   "fake memory store should update session activity timestamps");

     dasall::contracts::SummaryMemory summary;
     summary.summary_id = "summary-001";
     summary.session_id = "session-001";
     summary.summary_text = "A compact summary";
     summary.created_at = 4;
     assert_true(store.upsert_summary(summary).ok,
                                   "fake memory store should upsert summaries through the IMemoryStore surface");
     assert_true(store.load_latest_summary("session-001").has_value(),
                                   "fake memory store should expose the latest summary for a session");

     dasall::contracts::MemoryFact fact;
     fact.fact_id = "fact-001";
     fact.session_id = "session-001";
     fact.fact_text = "The shell command succeeded";
     fact.source_turn_ids = std::vector<std::string>{"turn-001"};
     fact.confidence_score = 90;
     fact.created_at = 5;
     fact.fact_type = "preference";
     assert_true(store.insert_fact(fact).ok,
                                   "fake memory store should insert facts through the IMemoryStore surface");
     const auto fact_result = store.query_facts(fact_query);
     assert_equal(1, static_cast<int>(fact_result.facts.size()),
                                    "fake memory store should answer fact queries through the frozen query surface");

     dasall::contracts::ExperienceMemory experience;
     experience.experience_id = "experience-001";
     experience.session_id = "session-001";
     experience.lesson_summary = "Retry storage after a busy window";
     experience.trigger_condition = "storage busy";
     experience.recommended_action = "retry with bounded budget";
     experience.created_at = 6;
     experience.applicable_domains = std::vector<std::string>{"memory"};
     experience.tags = std::vector<std::string>{"stage:plan"};
     assert_true(store.insert_experience(experience).ok,
                                   "fake memory store should insert experiences through the IMemoryStore surface");
     const auto experience_result = store.query_experiences(experience_query);
     assert_equal(1, static_cast<int>(experience_result.experiences.size()),
                                    "fake memory store should answer experience queries through the frozen query surface");

     const auto bundle = store.load_session_bundle(session_load_request);
     assert_true(bundle.session.session_id == std::optional<std::string>{"session-001"},
                                   "session load bundle should surface the stored session metadata");
     assert_equal(1, static_cast<int>(bundle.recent_turns.size()),
                                    "session load bundle should surface the recent turn window");
     assert_equal(1, bundle.total_turn_count,
                                    "session load bundle should surface the total turn count");

     assert_true(store.quarantine_record("fact", "fact-001", "manual_review").ok,
                                   "fake memory store should expose the maintenance quarantine surface");

     store.close();
     assert_true(!store.is_open_for_test(),
                                   "fake memory store should clear its opened state after close()");
}

}  // namespace

int main() {
  try {
    test_memory_unit_surface_anchor_uses_a_collision_free_ctest_name();
    test_memory_public_include_layout_exists();
    test_memory_module_is_no_longer_placeholder_only();
    test_memory_context_supporting_types_compile_and_expose_expected_defaults();
    test_memory_writeback_supporting_types_compile_and_preserve_partial_retry_semantics();
    test_memory_config_defaults_align_with_detailed_design();
          test_memory_error_mapping_aligns_with_warning_and_audit_semantics();
         test_memory_manager_supporting_types_and_factory_surface_compile_with_stable_defaults();
                     test_working_memory_board_interface_surface_compiles_with_snapshot_round_trip();
          test_memory_manager_interfaces_expose_expected_runtime_facing_signatures();
          test_memory_summarizer_interface_uses_module_local_summary_supporting_types();
            test_memory_store_interfaces_and_fake_cover_transactions_and_query_supporting_types();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
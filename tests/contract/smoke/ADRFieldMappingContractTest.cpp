#include <exception>
#include <iostream>
#include <string>

#include "boundary/ADRFieldMappingGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ADRIdentifier;
using dasall::contracts::ADRMappedObject;
using dasall::contracts::count_forbidden_field_mappings_for_adr;
using dasall::contracts::count_object_mappings_for_adr;
using dasall::contracts::has_adr_forbidden_field_mapping;
using dasall::contracts::is_adr_object_mapped;
using dasall::contracts::kADRFieldObjectMappingCatalog;
using dasall::contracts::kADRForbiddenFieldMappingCatalog;
using dasall::contracts::validate_adr_field_mapping_catalog;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_object_mapping_catalog_counts_match_t022_design() {
  assert_equal(10,
               static_cast<int>(kADRFieldObjectMappingCatalog.size()),
               "T022 object catalog must expose exactly 10 ADR-owned objects");

  assert_equal(3,
               static_cast<int>(count_object_mappings_for_adr(
                   ADRIdentifier::ADR006)),
               "ADR-006 object mapping count must remain 3");
  assert_equal(3,
               static_cast<int>(count_object_mappings_for_adr(
                   ADRIdentifier::ADR007)),
               "ADR-007 object mapping count must remain 3");
  assert_equal(4,
               static_cast<int>(count_object_mappings_for_adr(
                   ADRIdentifier::ADR008)),
               "ADR-008 object mapping count must remain 4");
}

void test_object_mapping_catalog_covers_all_wp04_objects() {
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR006,
                                   ADRMappedObject::ContextPacket),
              "ADR-006 must map ContextPacket");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR006,
                                   ADRMappedObject::PromptComposeRequest),
              "ADR-006 must map PromptComposeRequest");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR006,
                                   ADRMappedObject::PromptComposeResult),
              "ADR-006 must map PromptComposeResult");

  assert_true(is_adr_object_mapped(ADRIdentifier::ADR007,
                                   ADRMappedObject::ReflectionDecision),
              "ADR-007 must map ReflectionDecision");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR007,
                                   ADRMappedObject::RecoveryRequest),
              "ADR-007 must map RecoveryRequest");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR007,
                                   ADRMappedObject::RecoveryOutcome),
              "ADR-007 must map RecoveryOutcome");

  assert_true(is_adr_object_mapped(ADRIdentifier::ADR008,
                                   ADRMappedObject::MultiAgentRequest),
              "ADR-008 must map MultiAgentRequest");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR008,
                                   ADRMappedObject::MultiAgentResult),
              "ADR-008 must map MultiAgentResult");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR008,
                                   ADRMappedObject::WorkerTask),
              "ADR-008 must map WorkerTask");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR008,
                                   ADRMappedObject::WorkerLease),
              "ADR-008 must map WorkerLease");
}

void test_forbidden_field_catalog_counts_match_t022_design() {
  assert_equal(57,
               static_cast<int>(kADRForbiddenFieldMappingCatalog.size()),
               "T022 forbidden-field catalog must expose exactly 57 mappings");

  assert_equal(17,
               static_cast<int>(count_forbidden_field_mappings_for_adr(
                   ADRIdentifier::ADR006)),
               "ADR-006 forbidden-field mapping count must remain 17");
  assert_equal(24,
               static_cast<int>(count_forbidden_field_mappings_for_adr(
                   ADRIdentifier::ADR007)),
               "ADR-007 forbidden-field mapping count must remain 24");
  assert_equal(16,
               static_cast<int>(count_forbidden_field_mappings_for_adr(
                   ADRIdentifier::ADR008)),
               "ADR-008 forbidden-field mapping count must remain 16");
}

void test_representative_field_lookups_cover_three_adr_waves() {
  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR006,
                                              ADRMappedObject::ContextPacket,
                                              "rendered_prompt"),
              "ADR-006 must map ContextPacket.rendered_prompt");
  assert_true(has_adr_forbidden_field_mapping(
                  ADRIdentifier::ADR006,
                  ADRMappedObject::PromptComposeResult,
                  "memory_write_back"),
              "ADR-006 must map PromptComposeResult.memory_write_back");

  assert_true(has_adr_forbidden_field_mapping(
                  ADRIdentifier::ADR007,
                  ADRMappedObject::ReflectionDecision,
                  "retry_after_ms"),
              "ADR-007 must map ReflectionDecision.retry_after_ms");
  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR007,
                                              ADRMappedObject::RecoveryRequest,
                                              "executed_action"),
              "ADR-007 must map RecoveryRequest.executed_action");
  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR007,
                                              ADRMappedObject::RecoveryOutcome,
                                              "plan_patch_hint"),
              "ADR-007 must map RecoveryOutcome.plan_patch_hint");

  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR008,
                                              ADRMappedObject::MultiAgentRequest,
                                              "agent_request"),
              "ADR-008 must map MultiAgentRequest.agent_request");
  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR008,
                                              ADRMappedObject::WorkerTask,
                                              "global_fsm_state"),
              "ADR-008 must map WorkerTask.global_fsm_state");
  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR008,
                                              ADRMappedObject::WorkerLease,
                                              "resume_token"),
              "ADR-008 must map WorkerLease.resume_token");

  assert_true(!has_adr_forbidden_field_mapping(
                  ADRIdentifier::ADR006,
                  ADRMappedObject::ContextPacket,
                  "user_turn"),
              "allowed ContextPacket fields must not be misclassified as forbidden mappings");
}

void test_catalog_validation_passes() {
  const auto result = validate_adr_field_mapping_catalog();
  assert_true(result.ok,
              "T022 ADR field mapping catalog must pass completeness validation");
  assert_true(result.object_catalog_complete,
              "object catalog completeness flag must be true");
  assert_true(result.forbidden_field_catalog_complete,
              "forbidden-field catalog completeness flag must be true");
  assert_true(result.guard_dispatch_complete,
              "guard dispatch completeness flag must be true");
  assert_true(result.no_duplicate_entries,
              "duplicate-entry check must pass");
  assert_equal("none",
               std::string(result.first_failed_check),
               "successful validation must report no failed check");
}

}  // namespace

int main() {
  try {
    test_object_mapping_catalog_counts_match_t022_design();
    test_object_mapping_catalog_covers_all_wp04_objects();
    test_forbidden_field_catalog_counts_match_t022_design();
    test_representative_field_lookups_cover_three_adr_waves();
    test_catalog_validation_passes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::knowledge {

enum class KnowledgeErrorCode : std::uint16_t {
  Unspecified = 0,
  QueryValidationFailed = 1001,
  Disabled = 2001,
  IndexStaleRejected = 2002,
  EvidenceBudgetExhausted = 2003,
  NoCorpusAvailable = 4001,
  IndexUnavailable = 4002,
  VectorBackendUnavailable = 4003,
  RefreshFailed = 4004,
  NotInitialized = 5001,
  RecallTimeout = 5002,
  RefreshBusy = 5003,
  InternalError = 5004,
};

struct KnowledgeErrorDescriptor {
  dasall::contracts::ResultCodeCategory failure_type =
      dasall::contracts::ResultCodeCategory::Unknown;
  bool retryable = false;
  bool safe_to_replan = false;
  std::string_view source_ref_type = "knowledge::internal";
  std::string_view default_ref_id = "unspecified";
};

[[nodiscard]] inline constexpr std::string_view knowledge_error_code_name(
    KnowledgeErrorCode code) {
  switch (code) {
    case KnowledgeErrorCode::Unspecified:
      return "unspecified";
    case KnowledgeErrorCode::QueryValidationFailed:
      return "query_validation_failed";
    case KnowledgeErrorCode::Disabled:
      return "disabled";
    case KnowledgeErrorCode::IndexStaleRejected:
      return "index_stale_rejected";
    case KnowledgeErrorCode::EvidenceBudgetExhausted:
      return "evidence_budget_exhausted";
    case KnowledgeErrorCode::NoCorpusAvailable:
      return "no_corpus_available";
    case KnowledgeErrorCode::IndexUnavailable:
      return "index_unavailable";
    case KnowledgeErrorCode::VectorBackendUnavailable:
      return "vector_backend_unavailable";
    case KnowledgeErrorCode::RefreshFailed:
      return "refresh_failed";
    case KnowledgeErrorCode::NotInitialized:
      return "not_initialized";
    case KnowledgeErrorCode::RecallTimeout:
      return "recall_timeout";
    case KnowledgeErrorCode::RefreshBusy:
      return "refresh_busy";
    case KnowledgeErrorCode::InternalError:
      return "internal_error";
  }

  return "unspecified";
}

[[nodiscard]] inline constexpr KnowledgeErrorDescriptor describe_knowledge_error(
    KnowledgeErrorCode code) {
  switch (code) {
    case KnowledgeErrorCode::QueryValidationFailed:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Validation,
          .retryable = false,
          .safe_to_replan = false,
          .source_ref_type = "knowledge::normalizer",
          .default_ref_id = "query_validation_failed",
      };
    case KnowledgeErrorCode::Disabled:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Policy,
          .retryable = false,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::config",
          .default_ref_id = "disabled",
      };
    case KnowledgeErrorCode::IndexStaleRejected:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Policy,
          .retryable = true,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::freshness",
          .default_ref_id = "index_stale_rejected",
      };
    case KnowledgeErrorCode::EvidenceBudgetExhausted:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Policy,
          .retryable = false,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::assembler",
          .default_ref_id = "evidence_budget_exhausted",
      };
    case KnowledgeErrorCode::NoCorpusAvailable:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Provider,
          .retryable = false,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::router",
          .default_ref_id = "no_corpus_available",
      };
    case KnowledgeErrorCode::IndexUnavailable:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Provider,
          .retryable = true,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::index_reader",
          .default_ref_id = "index_unavailable",
      };
    case KnowledgeErrorCode::VectorBackendUnavailable:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Provider,
          .retryable = true,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::vector_bridge",
          .default_ref_id = "vector_backend_unavailable",
      };
    case KnowledgeErrorCode::RefreshFailed:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Provider,
          .retryable = true,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::index_writer",
          .default_ref_id = "refresh_failed",
      };
    case KnowledgeErrorCode::NotInitialized:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
          .retryable = true,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::facade",
          .default_ref_id = "not_initialized",
      };
    case KnowledgeErrorCode::RecallTimeout:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
          .retryable = true,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::recall_coordinator",
          .default_ref_id = "recall_timeout",
      };
    case KnowledgeErrorCode::RefreshBusy:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
          .retryable = true,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::ingest_worker",
          .default_ref_id = "refresh_busy",
      };
    case KnowledgeErrorCode::InternalError:
      return KnowledgeErrorDescriptor{
          .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
          .retryable = false,
          .safe_to_replan = true,
          .source_ref_type = "knowledge::internal",
          .default_ref_id = "internal_error",
      };
    case KnowledgeErrorCode::Unspecified:
      break;
  }

  return KnowledgeErrorDescriptor{};
}

[[nodiscard]] inline dasall::contracts::ErrorInfo make_knowledge_error_info(
    KnowledgeErrorCode code,
    std::string message,
    std::string stage,
    std::string ref_id = {}) {
  const auto descriptor = describe_knowledge_error(code);
  if (ref_id.empty()) {
    ref_id = std::string(descriptor.default_ref_id);
  }

  return dasall::contracts::ErrorInfo{
      .failure_type = descriptor.failure_type,
      .retryable = descriptor.retryable,
      .safe_to_replan = descriptor.safe_to_replan,
      .details = {
          .code = static_cast<int>(code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = {
          .ref_type = std::string(descriptor.source_ref_type),
          .ref_id = std::move(ref_id),
      },
  };
}

}  // namespace dasall::knowledge
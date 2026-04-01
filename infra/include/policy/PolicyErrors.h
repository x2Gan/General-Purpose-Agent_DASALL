#pragma once

#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::policy {

enum class PolicyErrorCode {
  BundleInvalid = 1,
  SchemaUnsupported = 2,
  ConflictUnresolved = 3,
  PatchBaseMismatch = 4,
  SnapshotNotFound = 5,
  RollbackFailed = 6,
  QueryDenied = 7,
  SourceUnavailable = 8,
  StoreCommitFailed = 9,
  DryRunRejected = 10,
};

struct PolicyErrorMapping {
  PolicyErrorCode policy_code;
  contracts::ResultCode result_code;
  std::string_view reason;
};

inline constexpr std::string_view policy_error_code_name(PolicyErrorCode code) {
  switch (code) {
    case PolicyErrorCode::BundleInvalid:
      return "INF_E_POLICY_BUNDLE_INVALID";
    case PolicyErrorCode::SchemaUnsupported:
      return "INF_E_POLICY_SCHEMA_UNSUPPORTED";
    case PolicyErrorCode::ConflictUnresolved:
      return "INF_E_POLICY_CONFLICT_UNRESOLVED";
    case PolicyErrorCode::PatchBaseMismatch:
      return "INF_E_POLICY_PATCH_BASE_MISMATCH";
    case PolicyErrorCode::SnapshotNotFound:
      return "INF_E_POLICY_SNAPSHOT_NOT_FOUND";
    case PolicyErrorCode::RollbackFailed:
      return "INF_E_POLICY_ROLLBACK_FAILED";
    case PolicyErrorCode::QueryDenied:
      return "INF_E_POLICY_QUERY_DENIED";
    case PolicyErrorCode::SourceUnavailable:
      return "INF_E_POLICY_SOURCE_UNAVAILABLE";
    case PolicyErrorCode::StoreCommitFailed:
      return "INF_E_POLICY_STORE_COMMIT_FAILED";
    case PolicyErrorCode::DryRunRejected:
      return "INF_E_POLICY_DRYRUN_REJECTED";
  }

  return "INF_E_POLICY_UNKNOWN";
}

inline constexpr PolicyErrorMapping map_policy_error_code(PolicyErrorCode code) {
  switch (code) {
    case PolicyErrorCode::BundleInvalid:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "invalid policy bundles stay inside contracts validation category",
      };
    case PolicyErrorCode::SchemaUnsupported:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "unsupported policy schemas stay inside contracts validation category",
      };
    case PolicyErrorCode::ConflictUnresolved:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .reason = "unresolved policy conflicts stay inside contracts policy category",
      };
    case PolicyErrorCode::PatchBaseMismatch:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .reason = "patch base mismatches stay inside contracts validation category",
      };
    case PolicyErrorCode::SnapshotNotFound:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "policy snapshot lookup failures stay inside contracts runtime category",
      };
    case PolicyErrorCode::RollbackFailed:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "policy rollback failures stay inside contracts runtime category",
      };
    case PolicyErrorCode::QueryDenied:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .reason = "policy query denials stay inside contracts policy category",
      };
    case PolicyErrorCode::SourceUnavailable:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::ProviderTimeout,
          .reason = "policy source availability failures stay inside contracts provider category",
      };
    case PolicyErrorCode::StoreCommitFailed:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .reason = "policy snapshot store commit failures stay inside contracts runtime category",
      };
    case PolicyErrorCode::DryRunRejected:
      return PolicyErrorMapping{
          .policy_code = code,
          .result_code = contracts::ResultCode::PolicyDenied,
          .reason = "policy dry-run rejections stay inside contracts policy category",
      };
  }

  return PolicyErrorMapping{
      .policy_code = code,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .reason = "unknown policy errors fall back to contracts runtime category",
  };
}

}  // namespace dasall::infra::policy
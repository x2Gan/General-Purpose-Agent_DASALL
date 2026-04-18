#pragma once

#include <cerrno>
#include <string_view>

#include "error/ResultCode.h"

namespace dasall::memory {

enum class MemoryError {
  StorageBusy = 1,
  SchemaMismatch = 2,
  ValidationRejected = 3,
  StorageUnavailable = 4,
  ConfigInvalid = 5,
};

struct MemoryErrorMapping {
  MemoryError memory_error;
  contracts::ResultCode result_code;
  std::string_view warning_key;
  std::string_view audit_scope;
  bool retryable = false;
  bool audit_required = false;
  std::string_view reason;
};

inline constexpr std::string_view memory_error_name(MemoryError error) {
  switch (error) {
    case MemoryError::StorageBusy:
      return "MEM_E_STORAGE_BUSY";
    case MemoryError::SchemaMismatch:
      return "MEM_E_SCHEMA_MISMATCH";
    case MemoryError::ValidationRejected:
      return "MEM_E_VALIDATION_REJECTED";
    case MemoryError::StorageUnavailable:
      return "MEM_E_STORAGE_UNAVAILABLE";
    case MemoryError::ConfigInvalid:
      return "MEM_E_CONFIG_INVALID";
  }

  return "MEM_E_UNKNOWN";
}

inline constexpr MemoryErrorMapping map_memory_error(MemoryError error) {
  switch (error) {
    case MemoryError::StorageBusy:
      return MemoryErrorMapping{
          .memory_error = error,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .warning_key = "retryable_storage_failure",
          .audit_scope = "runtime",
          .retryable = true,
          .audit_required = false,
          .reason = "local SQLite lock contention stays inside the contracts runtime failure category until the bounded retry window is exhausted",
      };
    case MemoryError::SchemaMismatch:
      return MemoryErrorMapping{
          .memory_error = error,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .warning_key = "non_retryable_init_write_failure",
          .audit_scope = "runtime/deploy",
          .retryable = false,
          .audit_required = true,
          .reason = "schema incompatibility is treated as a non-retryable validation-class failure and must emit audit evidence before memory init or write continues",
      };
    case MemoryError::ValidationRejected:
      return MemoryErrorMapping{
          .memory_error = error,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .warning_key = "partial_writeback_warning",
          .audit_scope = "memory",
          .retryable = false,
          .audit_required = true,
          .reason = "fact summary or experience guard rejections stay inside the validation failure category while allowing the main writeback path to continue in degraded partial mode",
      };
    case MemoryError::StorageUnavailable:
      return MemoryErrorMapping{
          .memory_error = error,
          .result_code = contracts::ResultCode::RuntimeRetryExhausted,
          .warning_key = "storage_unavailable",
          .audit_scope = "runtime",
          .retryable = false,
          .audit_required = true,
          .reason = "unavailable local storage blocks init or write progress and therefore maps to the contracts runtime failure category with explicit audit evidence",
      };
    case MemoryError::ConfigInvalid:
      return MemoryErrorMapping{
          .memory_error = error,
          .result_code = contracts::ResultCode::ValidationFieldMissing,
          .warning_key = "config_invalid",
          .audit_scope = "runtime",
          .retryable = false,
          .audit_required = true,
          .reason = "invalid memory configuration is treated as a validation-class failure until the projected config is corrected",
      };
  }

  return MemoryErrorMapping{
      .memory_error = error,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .warning_key = "unknown_memory_error",
      .audit_scope = "runtime",
      .retryable = false,
      .audit_required = true,
      .reason = "unknown memory errors fall back to the contracts runtime failure category",
  };
}

inline constexpr MemoryError map_memory_errno(int error_number) {
  switch (error_number) {
    case EBUSY:
    case EAGAIN:
      return MemoryError::StorageBusy;
    case EINVAL:
    case ENOENT:
      return MemoryError::ConfigInvalid;
    case EIO:
    case ENOSPC:
    case EROFS:
    case ETIMEDOUT:
      return MemoryError::StorageUnavailable;
    default:
      return MemoryError::StorageUnavailable;
  }
}

}  // namespace dasall::memory
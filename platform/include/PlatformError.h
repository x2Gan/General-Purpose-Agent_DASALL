#pragma once

#include <optional>
#include <string>

#include "error/ResultCode.h"

namespace dasall::platform {

enum class PlatformErrorCode {
  InvalidArgument,
  Timeout,
  ResourceExhausted,
  QueueClosed,
  NotFound,
  PermissionDenied,
  NoSpace,
  ConnectionRefused,
  Disconnected,
  AddressInUse,
  InternalFailure,
};

enum class PlatformErrorCategory {
  Validation,
  Resource,
  IO,
  Network,
  IPC,
  Internal,
};

struct PlatformError {
  PlatformErrorCode code = PlatformErrorCode::InternalFailure;
  PlatformErrorCategory category = PlatformErrorCategory::Internal;
  bool retryable_hint = false;
  std::string syscall_name;
  std::optional<int> errno_value;
  std::string detail;

  [[nodiscard]] bool has_consistent_values() const {
    if (detail.empty()) {
      return false;
    }

    if (syscall_name.empty() && errno_value.has_value()) {
      return false;
    }

    return true;
  }
};

[[nodiscard]] constexpr contracts::ResultCodeCategory map_platform_error_category_to_contracts(
    PlatformErrorCategory category) {
  switch (category) {
    case PlatformErrorCategory::Validation:
      return contracts::ResultCodeCategory::Validation;
    case PlatformErrorCategory::Resource:
      return contracts::ResultCodeCategory::Runtime;
    case PlatformErrorCategory::IO:
      return contracts::ResultCodeCategory::Provider;
    case PlatformErrorCategory::Network:
      return contracts::ResultCodeCategory::Provider;
    case PlatformErrorCategory::IPC:
      return contracts::ResultCodeCategory::Provider;
    case PlatformErrorCategory::Internal:
      return contracts::ResultCodeCategory::Runtime;
  }

  return contracts::ResultCodeCategory::Unknown;
}

}  // namespace dasall::platform
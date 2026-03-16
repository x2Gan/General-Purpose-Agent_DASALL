#pragma once

#include <string_view>

namespace dasall::contracts {

// ResultCodeCategory is the frozen WP02-T004一级失败域。The enum order is
// intentional and should remain stable because tests use numeric casts for
// strict deterministic assertions.
enum class ResultCodeCategory {
  Validation = 0,
  Policy = 1,
  Tool = 2,
  Provider = 3,
  Runtime = 4,
  Unknown = 5,
};

// ResultCode provides a minimal executable seed set for the five categories.
// Values are placed in the T004 coding ranges to make category判定可程序化。
enum class ResultCode {
  ValidationFieldMissing = 1001,
  PolicyDenied = 2001,
  ToolExecutionFailed = 3001,
  ProviderTimeout = 4001,
  RuntimeRetryExhausted = 5001,
};

struct ResultCodeClassification {
  bool ok = false;
  ResultCodeCategory category = ResultCodeCategory::Unknown;
  std::string_view reason = "result code is outside WP02 known category ranges";
};

inline constexpr std::string_view result_code_category_name(ResultCodeCategory category) {
  switch (category) {
    case ResultCodeCategory::Validation:
      return "validation";
    case ResultCodeCategory::Policy:
      return "policy";
    case ResultCodeCategory::Tool:
      return "tool";
    case ResultCodeCategory::Provider:
      return "provider";
    case ResultCodeCategory::Runtime:
      return "runtime";
    case ResultCodeCategory::Unknown:
      return "unknown";
  }

  return "unknown";
}

// Classifies by frozen T004 segment boundaries.
// 1000-1999: validation
// 2000-2999: policy
// 3000-3999: tool
// 4000-4999: provider
// 5000-5999: runtime
inline constexpr ResultCodeCategory classify_result_code_segment(int raw_code) {
  if (raw_code >= 1000 && raw_code <= 1999) {
    return ResultCodeCategory::Validation;
  }

  if (raw_code >= 2000 && raw_code <= 2999) {
    return ResultCodeCategory::Policy;
  }

  if (raw_code >= 3000 && raw_code <= 3999) {
    return ResultCodeCategory::Tool;
  }

  if (raw_code >= 4000 && raw_code <= 4999) {
    return ResultCodeCategory::Provider;
  }

  if (raw_code >= 5000 && raw_code <= 5999) {
    return ResultCodeCategory::Runtime;
  }

  return ResultCodeCategory::Unknown;
}

inline constexpr ResultCodeCategory classify_result_code(ResultCode code) {
  return classify_result_code_segment(static_cast<int>(code));
}

// Runtime helper for gate-style checks where the raw integer value is the
// only input. Unknown/out-of-range codes are explicitly rejected.
inline constexpr ResultCodeClassification classify_result_code_value(int raw_code) {
  const auto category = classify_result_code_segment(raw_code);
  if (category == ResultCodeCategory::Unknown) {
    return ResultCodeClassification{};
  }

  return ResultCodeClassification{
      .ok = true,
      .category = category,
      .reason = "result code is within WP02 frozen category ranges",
  };
}

}  // namespace dasall::contracts

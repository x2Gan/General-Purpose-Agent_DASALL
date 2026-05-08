#include "config/PrivilegeProbe.h"

#include <unistd.h>

namespace dasall::apps::cli::config {
namespace {

constexpr std::string_view kReasonRootRequired =
    "root_required_for_config_write";

[[nodiscard]] bool plan_requires_root(const ConfigActionPlan& plan) {
  for (const auto& file_write : plan.file_writes) {
    if (file_write.requires_root) {
      return true;
    }
  }

  return !plan.secret_writes.empty() || plan.service_reload_required ||
         plan.service_restart_required || plan.service_start_requested ||
         plan.service_enable_requested;
}

}  // namespace

bool PrivilegeRequirementResult::has_reason(
    const std::string_view reason) const {
  for (const auto& existing : failure_reasons) {
    if (existing == reason) {
      return true;
    }
  }

  return false;
}

PrivilegeContext PrivilegeProbe::current() const {
  return PrivilegeContext{
      .running_as_root = ::geteuid() == 0,
      .stdin_is_tty = ::isatty(STDIN_FILENO) != 0,
  };
}

PrivilegeRequirementResult PrivilegeProbe::require_root_for_write(
    const ConfigActionPlan& plan,
    const PrivilegeContext& context) const {
  PrivilegeRequirementResult result;
  result.root_required = plan_requires_root(plan);
  result.allowed = !result.root_required || context.running_as_root;
  if (!result.allowed) {
    result.failure_reasons.emplace_back(kReasonRootRequired);
  }

  return result;
}

}  // namespace dasall::apps::cli::config
#include "diagnostics/EvidenceCollector.h"

#include <algorithm>
#include <string_view>

namespace dasall::infra::diagnostics {
namespace {

[[nodiscard]] std::string fallback_ref(std::string_view scheme, std::string_view command_name) {
  return std::string(scheme) + "://diagnostics/" + std::string(command_name);
}

[[nodiscard]] std::string pick_ref(const std::vector<std::string>& refs,
                                   std::string_view prefix,
                                   std::string fallback) {
  const auto match = std::find_if(refs.begin(), refs.end(), [prefix](const std::string& ref) {
    return ref.rfind(prefix, 0) == 0;
  });
  if (match != refs.end()) {
    return *match;
  }

  return fallback;
}

}  // namespace

EvidenceBundle EvidenceCollector::collect(const DiagnosticsCommand& command,
                                          const CommandExecutionResult& execution) const {
  EvidenceBundle bundle{
      .logs_ref = pick_ref(execution.evidence_refs,
                           "logs://",
                           fallback_ref("logs", command.command_name)),
      .metrics_ref = pick_ref(execution.evidence_refs,
                              "metrics://",
                              fallback_ref("metrics", command.command_name)),
      .health_ref = pick_ref(execution.evidence_refs,
                             "health://",
                             fallback_ref("health", command.command_name)),
      .errors_ref = execution.executed
                        ? std::string("errors://diagnostics/") + command.command_name + "/none"
                        : std::string("errors://diagnostics/") + command.command_name +
                              "/execute-failure",
      .artifacts = execution.evidence_refs,
  };

  if (!execution.command_ref.empty()) {
    bundle.artifacts.push_back(execution.command_ref);
  }

  if (execution.error.has_value()) {
    bundle.artifacts.push_back(std::string("error://diagnostics/") +
                               execution.error->details.stage);
  }

  return bundle;
}

}  // namespace dasall::infra::diagnostics
#pragma once

#include <string>
#include <string_view>

#include "CliCommandParser.h"
#include "CliIpcClient.h"

namespace dasall::apps::cli {

enum class CliPrimaryOutputStream {
  Stdout,
  Stderr,
};

enum class CliOutcomeFamily {
  Success,
  InvalidArguments,
  DaemonUnavailable,
  AccessDenied,
  BusinessFailure,
  RetryableFailure,
  ProtocolError,
};

struct CliExitDecision {
  int exit_code = 7;
  CliPrimaryOutputStream primary_output_stream = CliPrimaryOutputStream::Stderr;
  bool json_mode = false;
  CliOutcomeFamily outcome_family = CliOutcomeFamily::ProtocolError;
  std::string diagnostic_hint;
};

[[nodiscard]] CliExitDecision make_argument_error_decision(
    CliOutputMode output_mode = CliOutputMode::Human,
    std::string_view diagnostic_hint = "invalid arguments");

[[nodiscard]] CliExitDecision decide_exit_for_response(
    const DaemonClientResponse& response,
    CliOutputMode output_mode = CliOutputMode::Human);

}  // namespace dasall::apps::cli
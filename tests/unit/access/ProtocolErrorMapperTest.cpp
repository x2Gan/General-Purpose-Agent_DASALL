#include <exception>
#include <iostream>
#include <string>

#include "AccessErrors.h"
#include "ProtocolErrorMapper.h"
#include "agent/AgentResult.h"
#include "support/TestAssertions.h"

namespace {

void maps_agent_result_status_to_frozen_protocol_matrix() {
  using dasall::access::AccessErrorCode;
  using dasall::access::map_access_error;
  using dasall::access::map_agent_result_to_protocol;
  using dasall::contracts::AgentResult;
  using dasall::contracts::AgentResultStatus;
  using dasall::tests::support::assert_equal;

  AgentResult completed;
  completed.status = AgentResultStatus::Completed;
  const auto completed_mapping = map_agent_result_to_protocol(completed);
  assert_equal(200,
               completed_mapping.http_status,
               "completed result should map to 200 semantics");

  AgentResult timeout;
  timeout.status = AgentResultStatus::Timeout;
  const auto timeout_mapping = map_agent_result_to_protocol(timeout);
  const auto expected_timeout = map_access_error(AccessErrorCode::RuntimeDispatchTimeout);
  assert_equal(expected_timeout.http_status,
               timeout_mapping.http_status,
               "timeout result should reuse RuntimeDispatchTimeout mapping");

  AgentResult cancelled;
  cancelled.status = AgentResultStatus::Cancelled;
  const auto cancelled_mapping = map_agent_result_to_protocol(cancelled);
  const auto expected_cancelled = map_access_error(AccessErrorCode::CancellationFailed);
  assert_equal(expected_cancelled.http_status,
               cancelled_mapping.http_status,
               "cancelled result should reuse CancellationFailed mapping");
}

}  // namespace

int main() {
  try {
    maps_agent_result_status_to_frozen_protocol_matrix();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

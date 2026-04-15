#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "ToolInvocationContext.h"

namespace {

using dasall::tools::ToolConfirmationFact;
using dasall::tools::ToolInvocationContext;
using dasall::tools::ToolTraceContext;

static_assert(std::is_same_v<decltype(ToolTraceContext{}.trace_id),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolTraceContext{}.span_id),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolTraceContext{}.parent_span_id),
                             std::optional<std::string>>);

static_assert(std::is_same_v<decltype(ToolConfirmationFact{}.confirmation_id),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolConfirmationFact{}.subject_ref),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolConfirmationFact{}.proof_type),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolConfirmationFact{}.confirmed_at_ms),
                             std::optional<std::int64_t>>);

static_assert(std::is_same_v<decltype(ToolInvocationContext{}.caller_domain),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolInvocationContext{}.session_id),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolInvocationContext{}.profile_snapshot),
                             const dasall::profiles::RuntimePolicySnapshot*>);
static_assert(std::is_same_v<decltype(ToolInvocationContext{}.trace), ToolTraceContext>);
static_assert(std::is_same_v<decltype(ToolInvocationContext{}.confirmation_facts),
                             std::optional<std::vector<ToolConfirmationFact>>>);

void tool_invocation_context_surface_keeps_caller_profile_trace_and_confirmation_inputs() {
  const ToolInvocationContext context{
    .caller_domain = std::string("runtime"),
    .session_id = std::string("session-tools-002"),
    .profile_snapshot = nullptr,
    .trace = ToolTraceContext{
      .trace_id = std::string("trace-tools-002"),
      .span_id = std::string("span-tools-002"),
      .parent_span_id = std::string("span-tools-parent-001"),
    },
    .confirmation_facts = std::vector<ToolConfirmationFact>{
      ToolConfirmationFact{
        .confirmation_id = std::string("confirm-tools-002"),
        .subject_ref = std::string("action:terminal.exec"),
        .proof_type = std::string("user_ack"),
        .confirmed_at_ms = static_cast<std::int64_t>(1713139200000),
      },
    },
  };

  static_cast<void>(context);
}

}  // namespace
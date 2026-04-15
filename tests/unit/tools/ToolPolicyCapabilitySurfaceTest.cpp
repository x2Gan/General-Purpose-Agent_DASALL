#include <exception>
#include <iostream>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "ICapabilityCache.h"
#include "IPolicyGate.h"

namespace {

using dasall::tools::CapabilityEntry;
using dasall::tools::CapabilityFreshness;
using dasall::tools::CapabilitySnapshot;
using dasall::tools::ICapabilityCache;
using dasall::tools::IPolicyGate;
using dasall::tools::ToolAdmissionDecision;
using dasall::tools::ToolAdmissionEffect;
using dasall::tools::ToolAdmissionRequest;
using dasall::tools::ToolPolicyView;

static_assert(std::is_same_v<decltype(&IPolicyGate::evaluate),
                             ToolAdmissionDecision (IPolicyGate::*)(
                                 const ToolAdmissionRequest&, const ToolPolicyView&)>);
static_assert(std::is_abstract_v<IPolicyGate>);

static_assert(std::is_same_v<decltype(&ICapabilityCache::snapshot),
                             std::optional<CapabilitySnapshot> (ICapabilityCache::*)(
                                 std::string_view) const>);
static_assert(std::is_same_v<decltype(&ICapabilityCache::update),
                             void (ICapabilityCache::*)(CapabilitySnapshot)>);
static_assert(std::is_abstract_v<ICapabilityCache>);

void policy_and_capability_surfaces_keep_fail_closed_and_snapshot_only_shapes() {
  const ToolPolicyView policy_view{
    .effective_profile_id = std::string("edge_balanced"),
    .safe_mode_enabled = true,
    .high_risk_confirmation_required = true,
    .audit_level = std::string("strict"),
    .allowed_tool_domains = {"builtin", "mcp"},
    .tool_visibility_rules = {"allow:agent.*"},
  };

  const ToolAdmissionRequest request{
    .tool_name = std::string("agent.terminal"),
    .required_scopes = {"execution.command"},
    .caller_domain = std::string("runtime"),
    .high_risk = true,
    .confirmation_present = false,
    .route_proven = true,
  };

  const ToolAdmissionDecision decision{
    .effect = ToolAdmissionEffect::deny,
    .reason_code = std::string("MissingConfirmation"),
    .confirmation_required = true,
    .retryable = false,
  };

  const CapabilitySnapshot snapshot{
    .server_id = std::string("mcp://tools-host"),
    .entries = std::vector<CapabilityEntry>{
      CapabilityEntry{
        .capability_id = std::string("terminal.exec"),
        .capability_version = std::string("v1"),
        .tool_names = {"agent.terminal"},
      },
    },
    .freshness = CapabilityFreshness::fresh,
    .last_refresh_at_ms = 1713139200000,
    .last_error = std::nullopt,
    .trust_marker = std::string("trusted-local"),
  };

  static_cast<void>(policy_view);
  static_cast<void>(request);
  static_cast<void>(decision);
  static_cast<void>(snapshot);
}

}  // namespace

int main() {
  try {
    policy_and_capability_surfaces_keep_fail_closed_and_snapshot_only_shapes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
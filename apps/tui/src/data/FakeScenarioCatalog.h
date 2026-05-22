#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "data/ITuiDataSource.h"

namespace dasall::tui::data {

struct FakeScenario {
  std::string scenario_id;
  TuiSessionView session;
  TuiRouteCatalogView route_catalog;
  TuiSubmitTurnResult submit_result;
  std::vector<std::vector<TuiEventProjection>> event_batches;

  [[nodiscard]] bool has_consistent_values() const {
    return !scenario_id.empty() && !session.session_id.empty() &&
           !route_catalog.current_route.current_provider_id.empty() &&
           !route_catalog.current_route.current_model_id.empty() &&
           submit_result.has_consistent_values();
  }
};

struct FakeScenarioLoadResult {
  std::optional<FakeScenario> scenario;
  std::optional<TuiDataSourceIssue> issue;

  [[nodiscard]] bool ok() const {
    return scenario.has_value() && !issue.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    return scenario.has_value() != issue.has_value();
  }
};

namespace detail {

inline TuiDataSourceIssue make_issue(std::string reason_domain,
                                     std::string reason_code,
                                     std::string message,
                                     std::vector<std::pair<std::string, std::string>> metadata = {}) {
  TuiDataSourceIssue issue;
  issue.reason_domain = std::move(reason_domain);
  issue.reason_code = std::move(reason_code);
  issue.message = std::move(message);
  issue.metadata = std::move(metadata);
  return issue;
}

inline TuiSessionView make_session(std::string session_id,
                                   std::string profile_id,
                                   std::string startup_mode,
                                   std::string started_at) {
  TuiSessionView session;
  session.session_id = std::move(session_id);
  session.profile_id = std::move(profile_id);
  session.daemon_readiness = "ready";
  session.startup_mode = std::move(startup_mode);
  session.started_at = std::move(started_at);
  return session;
}

inline TuiTurnReceipt make_receipt(std::string request_id,
                                   std::string trace_id,
                                   std::string session_id,
                                   std::string disposition,
                                   std::string receipt_ref,
                                   std::string submitted_at,
                                   std::string summary_text,
                                   std::optional<std::string> reason_code = std::nullopt) {
  TuiTurnReceipt receipt;
  receipt.request_id = std::move(request_id);
  receipt.trace_id = std::move(trace_id);
  receipt.session_id = std::move(session_id);
  receipt.disposition = std::move(disposition);
  receipt.receipt_ref = std::move(receipt_ref);
  receipt.submitted_at = std::move(submitted_at);
  receipt.summary_text = std::move(summary_text);
  receipt.reason_code = std::move(reason_code);
  return receipt;
}

inline TuiStatusProjection make_status(std::string stage,
                                       std::string current_tool,
                                       std::string pending_interaction,
                                       std::string budget_summary,
                                       std::string recovery_summary,
                                       std::string health_summary,
                                       std::string safe_mode_summary) {
  TuiStatusProjection status;
  status.stage = std::move(stage);
  status.current_tool = std::move(current_tool);
  status.pending_interaction = std::move(pending_interaction);
  status.budget_summary = std::move(budget_summary);
  status.recovery_summary = std::move(recovery_summary);
  status.health_summary = std::move(health_summary);
  status.safe_mode_summary = std::move(safe_mode_summary);
  return status;
}

inline TuiToolSummaryView make_tool_summary(std::string tool_name,
                                            std::string risk_summary,
                                            std::string observation_summary,
                                            std::optional<int> latency_ms,
                                            std::vector<std::string> badges = {}) {
  TuiToolSummaryView tool_summary;
  tool_summary.tool_name = std::move(tool_name);
  tool_summary.risk_summary = std::move(risk_summary);
  tool_summary.observation_summary = std::move(observation_summary);
  tool_summary.latency_ms = latency_ms;
  tool_summary.badges = std::move(badges);
  return tool_summary;
}

inline TuiRouteCatalogEntry make_route_entry(std::string provider_id,
                                             std::string model_id,
                                             std::string depth_tier,
                                             bool selectable = true,
                                             std::vector<std::string> disabled_reasons = {}) {
  TuiRouteCatalogEntry route_entry;
  route_entry.provider_id = std::move(provider_id);
  route_entry.model_id = std::move(model_id);
  route_entry.depth_tier = std::move(depth_tier);
  route_entry.selectable = selectable;
  route_entry.disabled_reasons = std::move(disabled_reasons);
  return route_entry;
}

inline TuiRouteCatalogView make_route_catalog(std::string provider_id,
                                              std::string model_id,
                                              std::string depth_tier,
                                              std::vector<TuiRouteCatalogEntry> candidate_routes,
                                              std::vector<std::string> disabled_reasons = {}) {
  TuiRouteCatalogView route_catalog;
  route_catalog.current_route.current_provider_id = std::move(provider_id);
  route_catalog.current_route.current_model_id = std::move(model_id);
  route_catalog.current_route.current_depth_tier = std::move(depth_tier);
  route_catalog.current_route.next_preference.user_visible_summary = "auto";
  route_catalog.current_route.next_preference.source = "fake_scenario";
  route_catalog.candidate_routes = std::move(candidate_routes);
  route_catalog.disabled_reasons = std::move(disabled_reasons);
  return route_catalog;
}

inline TuiEventProjection make_event(std::string event_cursor,
                                     std::string event_kind,
                                     std::string session_id,
                                     std::string timestamp,
                                     std::optional<TuiStatusProjection> status_delta = std::nullopt,
                                     std::optional<TuiTurnReceipt> turn_receipt = std::nullopt,
                                     std::optional<TuiToolSummaryView> tool_summary = std::nullopt,
                                     std::optional<std::string> banner_reason = std::nullopt) {
  TuiEventProjection event;
  event.event_cursor = std::move(event_cursor);
  event.event_kind = std::move(event_kind);
  event.session_id = std::move(session_id);
  event.timestamp = std::move(timestamp);
  event.status_delta = std::move(status_delta);
  event.turn_receipt = std::move(turn_receipt);
  event.tool_summary = std::move(tool_summary);
  event.banner_reason = std::move(banner_reason);
  return event;
}

inline FakeScenario make_golden_ready() {
  FakeScenario scenario;
  scenario.scenario_id = "golden_ready";
  scenario.session = make_session("fake-golden-ready-001",
                                  "desktop_full",
                                  "full",
                                  "2026-05-22T10:00:00Z");
  scenario.route_catalog = make_route_catalog(
      "provider-openai",
      "gpt-4.1",
      "balanced",
      {make_route_entry("provider-openai", "gpt-4.1", "balanced"),
       make_route_entry("provider-openai", "gpt-4.1-mini", "fast"),
       make_route_entry("provider-anthropic", "claude-sonnet", "deep")});
  scenario.submit_result.receipt = make_receipt("fake-submit-golden-ready",
                                                "fake-trace-golden-ready",
                                                scenario.session.session_id,
                                                "accepted_async",
                                                "receipt-golden-ready",
                                                "2026-05-22T10:00:05Z",
                                                "Fake turn accepted for idle screen replay.");
  return scenario;
}

inline FakeScenario make_planning_tools() {
  FakeScenario scenario;
  scenario.scenario_id = "planning_tools";
  scenario.session = make_session("fake-planning-tools-001",
                                  "desktop_full",
                                  "full",
                                  "2026-05-22T10:01:00Z");
  scenario.route_catalog = make_route_catalog(
      "provider-openai",
      "gpt-4.1",
      "deep",
      {make_route_entry("provider-openai", "gpt-4.1", "deep"),
       make_route_entry("provider-openai", "gpt-4.1-mini", "fast"),
       make_route_entry("provider-anthropic", "claude-sonnet", "deep")});
  scenario.submit_result.receipt = make_receipt("fake-submit-planning-tools",
                                                "fake-trace-planning-tools",
                                                scenario.session.session_id,
                                                "accepted_async",
                                                "receipt-planning-tools",
                                                "2026-05-22T10:01:03Z",
                                                "Plan drafted and tool execution scheduled.");
  scenario.event_batches = {
      {make_event("planning-tools-001",
                  "status.updated",
                  scenario.session.session_id,
                  "2026-05-22T10:01:04Z",
                  make_status("planning",
                              "tool.search",
                              "",
                              "Budget 78% remaining",
                              "",
                              "healthy",
                              "normal"),
                  scenario.submit_result.receipt,
                  std::nullopt,
                  std::nullopt)},
      {make_event("planning-tools-002",
                  "tool.summary",
                  scenario.session.session_id,
                  "2026-05-22T10:01:05Z",
                  make_status("tool_calling",
                              "tool.search",
                              "",
                              "Budget 64% remaining",
                              "",
                              "healthy",
                              "normal"),
                  std::nullopt,
                  make_tool_summary("tool.search",
                                    "network-readonly",
                                    "Collected three planning references for the next turn.",
                                    148,
                                    {"planning", "busy"}),
                  std::string("Busy draft is locked while tool execution is in progress."))}};
  return scenario;
}

inline FakeScenario make_needs_confirm() {
  FakeScenario scenario;
  scenario.scenario_id = "needs_confirm";
  scenario.session = make_session("fake-needs-confirm-001",
                                  "desktop_full",
                                  "full",
                                  "2026-05-22T10:02:00Z");
  scenario.route_catalog = make_route_catalog(
      "provider-openai",
      "gpt-4.1",
      "balanced",
      {make_route_entry("provider-openai", "gpt-4.1", "balanced"),
       make_route_entry("provider-openai", "gpt-4.1-mini", "fast")});
  scenario.submit_result.receipt = make_receipt("fake-submit-needs-confirm",
                                                "fake-trace-needs-confirm",
                                                scenario.session.session_id,
                                                "accepted_async",
                                                "receipt-needs-confirm",
                                                "2026-05-22T10:02:03Z",
                                                "Awaiting confirmation before the next tool call.");
  scenario.event_batches = {{make_event("needs-confirm-001",
                                        "interaction.pending",
                                        scenario.session.session_id,
                                        "2026-05-22T10:02:04Z",
                                        make_status("waiting_interaction",
                                                    "",
                                                    "confirm_external_tool",
                                                    "Budget 52% remaining",
                                                    "",
                                                    "healthy",
                                                    "normal"),
                                        scenario.submit_result.receipt,
                                        std::nullopt,
                                        std::string("Confirmation required before dispatching the tool."))}};
  return scenario;
}

inline FakeScenario make_recovering() {
  FakeScenario scenario;
  scenario.scenario_id = "recovering";
  scenario.session = make_session("fake-recovering-001",
                                  "desktop_full",
                                  "full",
                                  "2026-05-22T10:03:00Z");
  scenario.route_catalog = make_route_catalog(
      "provider-anthropic",
      "claude-sonnet",
      "deep",
      {make_route_entry("provider-anthropic", "claude-sonnet", "deep"),
       make_route_entry("provider-openai", "gpt-4.1", "balanced")});
  scenario.submit_result.receipt = make_receipt("fake-submit-recovering",
                                                "fake-trace-recovering",
                                                scenario.session.session_id,
                                                "accepted_async",
                                                "receipt-recovering",
                                                "2026-05-22T10:03:03Z",
                                                "Recovery summary available for review.");
  scenario.event_batches = {{make_event("recovering-001",
                                        "recovery.updated",
                                        scenario.session.session_id,
                                        "2026-05-22T10:03:04Z",
                                        make_status("reflecting",
                                                    "",
                                                    "",
                                                    "Budget 48% remaining",
                                                    "Accepted safe replay window after tool timeout.",
                                                    "degraded",
                                                    "guarded"),
                                        scenario.submit_result.receipt,
                                        make_tool_summary("tool.timeout-review",
                                                          "timeout-isolated",
                                                          "Recovery accepted the bounded replay proposal.",
                                                          0,
                                                          {"recovery", "summary"}),
                                        std::string("Recovery summary updated for the current session."))}};
  return scenario;
}

inline FakeScenario make_route_switch() {
  FakeScenario scenario;
  scenario.scenario_id = "route_switch";
  scenario.session = make_session("fake-route-switch-001",
                                  "desktop_full",
                                  "full",
                                  "2026-05-22T10:04:00Z");
  scenario.route_catalog = make_route_catalog(
      "provider-openai",
      "gpt-4.1",
      "balanced",
      {make_route_entry("provider-openai", "gpt-4.1", "balanced"),
       make_route_entry("provider-openai",
                        "gpt-4.1-mini",
                        "fast",
                        true),
       make_route_entry("provider-anthropic",
                        "claude-sonnet",
                        "deep",
                        false,
                        {"credentials_missing"}),
       make_route_entry("provider-local",
                        "deep-reasoner",
                        "deep",
                        false,
                        {"verification_pending", "allowlist_blocked"})});
  scenario.route_catalog.current_route.next_preference.mode = TuiRoutePreferenceMode::PreferDepth;
  scenario.route_catalog.current_route.next_preference.preferred_depth_tier = std::string("deep");
  scenario.route_catalog.current_route.next_preference.user_visible_summary = "prefer depth";
  scenario.route_catalog.current_route.next_preference.source = "fake_selector";
  scenario.submit_result.receipt = make_receipt("fake-submit-route-switch",
                                                "fake-trace-route-switch",
                                                scenario.session.session_id,
                                                "accepted_async",
                                                "receipt-route-switch",
                                                "2026-05-22T10:04:03Z",
                                                "Selector preview accepted for the next turn.");
  scenario.event_batches = {{make_event("route-switch-001",
                                        "banner.updated",
                                        scenario.session.session_id,
                                        "2026-05-22T10:04:04Z",
                                        make_status("ready",
                                                    "",
                                                    "",
                                                    "Budget 81% remaining",
                                                    "",
                                                    "healthy",
                                                    "normal"),
                                        scenario.submit_result.receipt,
                                        std::nullopt,
                                        std::string("Selector preview updated with disabled reason hints."))}};
  return scenario;
}

inline FakeScenario make_narrow_cjk() {
  FakeScenario scenario;
  scenario.scenario_id = "narrow_cjk";
  scenario.session = make_session("fake-narrow-cjk-001",
                                  "desktop_cn",
                                  "narrow",
                                  "2026-05-22T10:05:00Z");
  scenario.route_catalog = make_route_catalog(
      "provider-openai",
      "gpt-4.1-mini",
      "fast",
      {make_route_entry("provider-openai", "gpt-4.1-mini", "fast"),
       make_route_entry("provider-openai", "gpt-4.1", "balanced")});
  scenario.submit_result.receipt = make_receipt("fake-submit-narrow-cjk",
                                                "fake-trace-narrow-cjk",
                                                scenario.session.session_id,
                                                "accepted_async",
                                                "receipt-narrow-cjk",
                                                "2026-05-22T10:05:03Z",
                                                "已接受：准备生成中文摘要。",
                                                std::nullopt);
  scenario.event_batches = {{make_event("narrow-cjk-001",
                                        "tool.summary",
                                        scenario.session.session_id,
                                        "2026-05-22T10:05:04Z",
                                        make_status("ready",
                                                    "tool.retrieve",
                                                    "",
                                                    "预算剩余 66%",
                                                    "",
                                                    "healthy",
                                                    "normal"),
                                        scenario.submit_result.receipt,
                                        make_tool_summary("tool.retrieve",
                                                          "只读检索",
                                                          "中文上下文片段已收集，可用于窄屏回放。",
                                                          92,
                                                          {"cjk", "narrow"}),
                                        std::string("中文摘要已更新，需检查 80x24 截断策略。"))}};
  return scenario;
}

}  // namespace detail

class FakeScenarioCatalog {
 public:
  [[nodiscard]] static constexpr std::array<std::string_view, 6> scenario_ids() {
    return {"golden_ready",
            "planning_tools",
            "needs_confirm",
            "recovering",
            "route_switch",
            "narrow_cjk"};
  }

  [[nodiscard]] static FakeScenarioLoadResult load(std::string_view scenario_id) {
    if (scenario_id == "golden_ready") {
      return FakeScenarioLoadResult{detail::make_golden_ready(), std::nullopt};
    }
    if (scenario_id == "planning_tools") {
      return FakeScenarioLoadResult{detail::make_planning_tools(), std::nullopt};
    }
    if (scenario_id == "needs_confirm") {
      return FakeScenarioLoadResult{detail::make_needs_confirm(), std::nullopt};
    }
    if (scenario_id == "recovering") {
      return FakeScenarioLoadResult{detail::make_recovering(), std::nullopt};
    }
    if (scenario_id == "route_switch") {
      return FakeScenarioLoadResult{detail::make_route_switch(), std::nullopt};
    }
    if (scenario_id == "narrow_cjk") {
      return FakeScenarioLoadResult{detail::make_narrow_cjk(), std::nullopt};
    }

    return FakeScenarioLoadResult{
        std::nullopt,
        detail::make_issue("request",
                           "validation_failed",
                           "Unknown fake TUI scenario requested.",
                           {{"scenario_id", std::string(scenario_id)}})};
  }
};

}  // namespace dasall::tui::data
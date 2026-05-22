#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dasall::tui::data {

enum class TuiRoutePreferenceMode {
  Auto,
  PreferDepth,
  PinModel,
};

struct NextTurnPreference {
  TuiRoutePreferenceMode mode = TuiRoutePreferenceMode::Auto;
  std::optional<std::string> preferred_depth_tier;
  std::optional<std::string> pinned_provider_id;
  std::optional<std::string> pinned_model_id;
  std::string user_visible_summary;
  std::string source;
  bool applies_to_next_turn_only = true;
};

struct TuiSessionView {
  std::string session_id;
  std::string profile_id;
  std::string daemon_readiness;
  std::string startup_mode;
  std::string started_at;
};

struct TuiTurnReceipt {
  std::string request_id;
  std::string trace_id;
  std::string session_id;
  std::string disposition;
  std::string receipt_ref;
  std::string submitted_at;
  std::string summary_text;
  std::optional<std::string> reason_code;
};

struct TuiStatusProjection {
  std::string stage;
  std::string current_tool;
  std::string pending_interaction;
  std::string budget_summary;
  std::string recovery_summary;
  std::string health_summary;
  std::string safe_mode_summary;
};

struct TuiToolSummaryView {
  std::string tool_name;
  std::string risk_summary;
  std::string observation_summary;
  std::optional<int> latency_ms;
  std::vector<std::string> badges;
};

struct TuiModelRouteProjection {
  std::string current_provider_id;
  std::string current_model_id;
  std::string current_depth_tier;
  std::vector<std::string> disabled_reasons;
  NextTurnPreference next_preference;
};

struct TuiRouteCatalogEntry {
  std::string provider_id;
  std::string model_id;
  std::string depth_tier;
  bool selectable = true;
  std::vector<std::string> disabled_reasons;
};

struct TuiRouteCatalogView {
  TuiModelRouteProjection current_route;
  std::vector<TuiRouteCatalogEntry> candidate_routes;
  std::vector<std::string> disabled_reasons;
};

struct TuiEventProjection {
  std::string event_cursor;
  std::string event_kind;
  std::string session_id;
  std::string timestamp;
  std::optional<TuiStatusProjection> status_delta;
  std::optional<TuiTurnReceipt> turn_receipt;
  std::optional<TuiToolSummaryView> tool_summary;
  std::optional<std::string> banner_reason;
};

}  // namespace dasall::tui::data
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct ValidationResult {
  bool ok = true;
  std::string failure;
};

std::string trim(const std::string& value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1U);
}

std::vector<std::string> split_lines(const std::string& document) {
  std::vector<std::string> lines;
  std::string current;

  for (const char ch : document) {
    if (ch == '\n') {
      lines.push_back(current);
      current.clear();
      continue;
    }

    if (ch != '\r') {
      current.push_back(ch);
    }
  }

  lines.push_back(current);
  return lines;
}

std::set<std::string> collect_key_paths(const std::string& document) {
  std::vector<std::pair<int, std::string>> stack;
  std::set<std::string> paths;

  for (const std::string& raw_line : split_lines(document)) {
    const std::string line = trim(raw_line);
    if (line.empty() || line.front() == '#') {
      continue;
    }

    const auto first_non_space = raw_line.find_first_not_of(" \t");
    const int indent = first_non_space == std::string::npos ? 0 : static_cast<int>(first_non_space);

    if (line.rfind("- ", 0) == 0) {
      continue;
    }

    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    const std::string key = trim(line.substr(0, colon));
    if (key.empty()) {
      continue;
    }

    while (!stack.empty() && stack.back().first >= indent) {
      stack.pop_back();
    }

    std::string path;
    for (const auto& [existing_indent, existing_key] : stack) {
      static_cast<void>(existing_indent);
      if (!path.empty()) {
        path.append(".");
      }
      path.append(existing_key);
    }

    if (!path.empty()) {
      path.append(".");
    }
    path.append(key);
    paths.insert(path);

    const std::string value = trim(line.substr(colon + 1U));
    if (value.empty()) {
      stack.emplace_back(indent, key);
    }
  }

  return paths;
}

std::set<std::string> collect_top_level_keys(const std::string& document) {
  std::set<std::string> result;
  for (const std::string& path : collect_key_paths(document)) {
    const auto dot = path.find('.');
    result.insert(dot == std::string::npos ? path : path.substr(0, dot));
  }
  return result;
}

bool contains_text(const std::string& document, const std::string& fragment) {
  return document.find(fragment) != std::string::npos;
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  assert_true(input.is_open(), "failed to open runtime policy file: " + path.string());
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

ValidationResult validate_required_paths(const std::string& document) {
  static const std::vector<std::string> required_paths = {
      "schema_version",
      "profile_meta",
      "profile_meta.profile_id",
      "profile_meta.target_platform",
      "profile_meta.support_level",
      "enabled_modules",
      "enabled_modules.runtime",
      "enabled_modules.cognition",
      "enabled_modules.llm_cloud_adapter",
      "enabled_modules.llm_lan_adapter",
      "enabled_modules.llm_local_adapter",
      "enabled_modules.tools_builtin",
      "enabled_modules.tools_mcp",
      "enabled_modules.memory_vector",
      "enabled_modules.memory_experience",
      "enabled_modules.knowledge",
      "enabled_modules.multi_agent",
      "enabled_modules.platform_hal",
      "enabled_modules.infra_observability",
      "runtime_budget",
      "runtime_budget.worker_threads",
      "runtime_budget.max_memory_mb",
      "runtime_budget.max_tokens",
      "runtime_budget.max_turns",
      "runtime_budget.max_tool_calls",
      "runtime_budget.max_latency_ms",
      "runtime_budget.max_replan_count",
      "model_profile",
      "model_profile.planner",
      "model_profile.planner.route",
      "model_profile.planner.fallback_route",
      "model_profile.planner.streaming_enabled",
      "model_profile.responder",
      "model_profile.responder.route",
      "model_profile.responder.fallback_route",
      "model_profile.responder.streaming_enabled",
      "token_budget_policy",
      "token_budget_policy.max_input_tokens",
      "token_budget_policy.max_output_tokens",
      "token_budget_policy.max_history_turns",
      "token_budget_policy.compression_threshold",
      "prompt_policy",
      "prompt_policy.allowed_prompt_releases",
      "prompt_policy.trusted_sources",
      "prompt_policy.tool_visibility_rules",
      "capability_cache_policy",
      "capability_cache_policy.refresh_interval_ms",
      "capability_cache_policy.expire_after_ms",
      "capability_cache_policy.stale_read_allowed",
      "capability_cache_policy.failure_backoff_ms",
      "degrade_policy",
      "degrade_policy.fallback_chain",
      "degrade_policy.allow_model_failover",
      "degrade_policy.allow_budget_degrade",
      "timeout_policy",
      "timeout_policy.llm",
      "timeout_policy.llm.timeout_ms",
      "timeout_policy.llm.retry_budget",
      "timeout_policy.llm.circuit_breaker_threshold",
      "timeout_policy.tool",
      "timeout_policy.tool.timeout_ms",
      "timeout_policy.tool.retry_budget",
      "timeout_policy.tool.circuit_breaker_threshold",
      "timeout_policy.mcp",
      "timeout_policy.mcp.timeout_ms",
      "timeout_policy.mcp.retry_budget",
      "timeout_policy.mcp.circuit_breaker_threshold",
      "timeout_policy.workflow",
      "timeout_policy.workflow.timeout_ms",
      "timeout_policy.workflow.retry_budget",
      "timeout_policy.workflow.circuit_breaker_threshold",
      "execution_policy",
      "execution_policy.requires_high_risk_confirmation",
      "execution_policy.safe_mode_enabled",
      "execution_policy.audit_level",
      "execution_policy.allowed_tool_domains",
      "infra",
      "infra.plugin",
      "infra.plugin.enabled",
      "infra.plugin.allowlist",
      "infra.plugin.search_paths",
      "infra.plugin.load_timeout_ms",
      "infra.plugin.max_active",
      "infra.plugin.signature",
      "infra.plugin.signature.required",
      "infra.plugin.trust",
      "infra.plugin.trust.min_level",
      "infra.plugin.abi",
      "infra.plugin.abi.strict_mode",
      "infra.plugin.remote_fetch",
      "infra.plugin.remote_fetch.enabled",
      "infra.plugin.safe_mode",
      "infra.plugin.safe_mode.fail_threshold",
      "infra.health",
      "infra.health.enabled",
      "infra.health.liveness",
      "infra.health.liveness.interval_ms",
      "infra.health.readiness",
      "infra.health.readiness.interval_ms",
      "infra.health.probe",
      "infra.health.probe.timeout_ms",
      "infra.health.probe.groups",
      "infra.health.probe.groups.critical",
      "infra.health.degraded",
      "infra.health.degraded.threshold",
      "infra.health.unhealthy",
      "infra.health.unhealthy.consecutive_failures",
      "infra.health.history",
      "infra.health.history.window_size",
      "infra.health.event_on_transition_only",
      "infra.health.recovery_hint",
      "infra.health.recovery_hint.enabled",
      "infra.metrics",
      "infra.metrics.enabled",
      "infra.metrics.provider",
      "infra.metrics.provider.type",
      "infra.metrics.reader",
      "infra.metrics.reader.interval_ms",
      "infra.metrics.exporter",
      "infra.metrics.exporter.type",
      "infra.metrics.exporter.timeout_ms",
      "infra.metrics.aggregation",
      "infra.metrics.aggregation.temporality",
      "infra.metrics.queue",
      "infra.metrics.queue.max_size",
      "infra.metrics.queue.overflow_policy",
      "infra.metrics.labels",
      "infra.metrics.labels.allowlist",
      "infra.metrics.labels.max_cardinality_per_metric",
      "infra.metrics.histogram",
      "infra.metrics.histogram.default_buckets_seconds",
      "infra.metrics.audit",
      "infra.metrics.audit.on_policy_change",
      "infra.watchdog",
      "infra.watchdog.enabled",
      "infra.watchdog.scan",
      "infra.watchdog.scan.interval_ms",
      "infra.watchdog.timeout_ms",
      "infra.watchdog.grace_ms",
      "infra.watchdog.consecutive_miss_threshold",
      "infra.watchdog.timeout",
      "infra.watchdog.timeout.level",
      "infra.watchdog.timeout.level.policy",
      "infra.watchdog.event",
      "infra.watchdog.event.queue_size",
      "infra.watchdog.event.overflow_policy",
      "infra.watchdog.recovery_hint",
      "infra.watchdog.recovery_hint.enabled",
      "infra.watchdog.audit",
      "infra.watchdog.audit.required",
      "infra.watchdog.max_entities",
      "infra.watchdog.safe_mode",
      "infra.watchdog.safe_mode.scan_interval_ms",
      "ops_policy",
      "ops_policy.log_level",
      "ops_policy.metrics_granularity",
      "ops_policy.trace_sample_ratio",
      "ops_policy.remote_diagnostics_enabled",
      "ops_policy.upgrade_strategy",
  };

  const std::set<std::string> actual_paths = collect_key_paths(document);
  for (const std::string& required_path : required_paths) {
    if (!actual_paths.contains(required_path)) {
      return ValidationResult{false, "missing required path: " + required_path};
    }
  }

  if (!contains_text(document, "schema_version: 1")) {
    return ValidationResult{false, "schema_version must stay frozen at 1"};
  }

  return ValidationResult{};
}

std::filesystem::path source_root() {
  return std::filesystem::path(DASALL_SOURCE_DIR);
}

void test_runtime_policy_documents_keep_required_schema() {
  const std::vector<std::pair<std::string, std::filesystem::path>> profiles = {
      {"desktop_full", source_root() / "profiles/desktop_full/runtime_policy.yaml"},
      {"cloud_full", source_root() / "profiles/cloud_full/runtime_policy.yaml"},
      {"edge_balanced", source_root() / "profiles/edge_balanced/runtime_policy.yaml"},
      {"edge_minimal", source_root() / "profiles/edge_minimal/runtime_policy.yaml"},
      {"factory_test", source_root() / "profiles/factory_test/runtime_policy.yaml"},
  };

  std::set<std::string> baseline_top_level_keys;
  for (const auto& [profile_id, path] : profiles) {
    const std::string document = read_text_file(path);
    const auto validation = validate_required_paths(document);
    assert_true(validation.ok, profile_id + " schema validation failed: " + validation.failure);
    assert_true(contains_text(document, "profile_id: " + profile_id),
                profile_id + " must declare matching profile_id");

    const std::set<std::string> top_level_keys = collect_top_level_keys(document);
    if (baseline_top_level_keys.empty()) {
      baseline_top_level_keys = top_level_keys;
      continue;
    }

    assert_true(top_level_keys == baseline_top_level_keys,
                profile_id + " top-level field set must remain aligned across all profiles");
  }
}

void test_profile_matrix_freeze_matches_blueprint_baselines() {
  const std::string desktop_full =
      read_text_file(source_root() / "profiles/desktop_full/runtime_policy.yaml");
  const std::string cloud_full =
      read_text_file(source_root() / "profiles/cloud_full/runtime_policy.yaml");
  const std::string edge_balanced =
      read_text_file(source_root() / "profiles/edge_balanced/runtime_policy.yaml");
  const std::string edge_minimal =
      read_text_file(source_root() / "profiles/edge_minimal/runtime_policy.yaml");
  const std::string factory_test =
      read_text_file(source_root() / "profiles/factory_test/runtime_policy.yaml");

  assert_true(contains_text(desktop_full, "multi_agent: true"),
              "desktop_full must keep multi_agent enabled");
  assert_true(contains_text(cloud_full, "llm_local_adapter: false"),
              "cloud_full must keep local adapter disabled in the baseline");
  assert_true(contains_text(edge_balanced, "llm_lan_adapter: true"),
              "edge_balanced must keep LAN adapter enabled");
  assert_true(contains_text(edge_balanced, "multi_agent: false"),
              "edge_balanced baseline freezes multi_agent off until optional path is implemented");
  assert_true(contains_text(edge_balanced, "plugin.edge.telemetry"),
              "edge_balanced must keep the edge telemetry plugin in the allowlist baseline");
  assert_true(contains_text(cloud_full, "max_size: 8192"),
              "cloud_full must keep the larger metrics queue ceiling for server deployments");
  assert_true(contains_text(cloud_full, "max_entities: 2048"),
              "cloud_full must keep the higher watchdog entity ceiling for server deployments");
  assert_true(contains_text(desktop_full, "max_cardinality_per_metric: 300"),
              "desktop_full must keep the desktop metrics cardinality ceiling baseline");
  assert_true(contains_text(edge_balanced, "type: prom_text"),
              "edge_balanced must keep prom_text exporter enabled in the balanced observability baseline");
  assert_true(contains_text(edge_minimal, "llm_cloud_adapter: false"),
              "edge_minimal must keep cloud adapter disabled");
  assert_true(contains_text(edge_minimal, "tools_mcp: false"),
              "edge_minimal must keep MCP tools disabled");
  assert_true(contains_text(edge_minimal, "plugin.echo"),
              "edge_minimal must keep a single minimal plugin allowlist baseline");
  assert_true(contains_text(edge_minimal, "type: noop"),
              "edge_minimal must keep the noop metrics exporter to avoid external export cost");
  assert_true(contains_text(edge_minimal, "window_size: 12"),
              "edge_minimal must keep a smaller health history window for constrained memory baselines");
  assert_true(contains_text(edge_minimal, "policy: critical_only"),
              "edge_minimal must keep watchdog timeout policy at critical_only to reduce advisory noise");
  assert_true(contains_text(factory_test, "platform_hal: true"),
              "factory_test must keep HAL enabled");
  assert_true(contains_text(factory_test, "remote_diagnostics_enabled: true"),
              "factory_test must keep remote diagnostics enabled");
  assert_true(contains_text(factory_test, "consecutive_failures: 2"),
              "factory_test must keep a tighter health unhealthy threshold for diagnostic loops");
  assert_true(contains_text(factory_test, "consecutive_miss_threshold: 2"),
              "factory_test must keep the tighter watchdog consecutive miss threshold for diagnostic loops");
  assert_true(contains_text(desktop_full, "plugin.tools.bridge"),
              "desktop_full must keep the desktop bridge plugin in the allowlist baseline");
  assert_true(contains_text(desktop_full, "queue_size: 2048"),
              "desktop_full must keep the default watchdog event queue capacity baseline");
  assert_true(contains_text(cloud_full, "plugin.remote.fetch"),
              "cloud_full must keep the remote fetch plugin id reserved in the allowlist baseline");
}

void test_missing_required_fields_are_rejected() {
  const std::string malformed_document = R"(schema_version: 1
profile_meta:
  profile_id: edge_minimal
enabled_modules:
  runtime: true
)";

  const auto validation = validate_required_paths(malformed_document);
  assert_true(!validation.ok, "malformed runtime policy must be rejected");
  assert_equal(std::string("missing required path: profile_meta.target_platform"),
               validation.failure,
               "validator should report the first missing frozen field");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  auto run_test = [&](const char* name, void (*fn)()) {
    try {
      fn();
      ++passed;
      std::cout << "  PASS: " << name << "\n";
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "  FAIL: " << name << " - " << ex.what() << "\n";
    }
  };

  std::cout << "ProfileRuntimePolicySchemaContractTest - PRF-TODO-013/022\n";

  run_test("test_runtime_policy_documents_keep_required_schema",
           test_runtime_policy_documents_keep_required_schema);
  run_test("test_profile_matrix_freeze_matches_blueprint_baselines",
           test_profile_matrix_freeze_matches_blueprint_baselines);
  run_test("test_missing_required_fields_are_rejected",
           test_missing_required_fields_are_rejected);

  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
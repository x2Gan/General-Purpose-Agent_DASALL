#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IPolicyGate.h"
#include "execution/WorkflowEngine.h"
#include "skills/SkillRegistry.h"

namespace dasall::tools::skills {

struct SkillInstance {
  std::string instance_id;
  SkillSpecAsset asset;
  std::vector<std::string> tool_allowlist;
  std::string workflow_template_ref;
  std::optional<std::string> prompt_bundle_ref;
  std::string eval_suite_ref;
  std::string fallback_strategy;
};

struct SkillInstantiateResult {
  bool instantiated = false;
  std::optional<SkillInstance> instance;
  std::optional<execution::WorkflowPlan> workflow_plan;
  std::string reason_code;
  std::vector<std::string> denied_tools;
  std::optional<std::string> fallback_strategy;
};

class SkillRuntime {
 public:
  SkillRuntime();
  explicit SkillRuntime(std::filesystem::path project_root);

  [[nodiscard]] SkillInstantiateResult instantiate(
      const SkillMatchResult& match_result,
      const ToolPolicyView& policy_view);
  [[nodiscard]] std::optional<execution::WorkflowPlan> bind_workflow_template(
      const SkillInstance& instance,
      std::string& reason_code) const;
  [[nodiscard]] std::vector<std::string> build_tool_allowlist(
      const SkillSpecAsset& asset,
      const ToolPolicyView& policy_view) const;
  [[nodiscard]] bool release_instance(std::string_view instance_id);

 private:
  [[nodiscard]] static std::filesystem::path default_project_root();
  [[nodiscard]] std::filesystem::path resolve_asset_path(
      std::string_view asset_ref) const;
  [[nodiscard]] std::string allocate_instance_id();

  std::filesystem::path project_root_;
  std::uint64_t next_instance_id_ = 1U;
  std::mutex instance_mutex_;
  std::map<std::string, SkillInstance> active_instances_;
};

}  // namespace dasall::tools::skills
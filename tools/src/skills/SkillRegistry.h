#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::tools::skills {

struct SkillSpecAsset {
  std::string skill_id;
  std::string source_key;
  std::string asset_ref;
  std::string version;
  std::string name;
  std::string description;
  std::vector<std::string> intent_patterns;
  std::vector<std::string> tags;
  std::vector<std::string> allowed_tools;
  std::vector<std::string> profile_constraints;
  std::vector<std::string> required_domains;
  std::string workflow_template_ref;
  std::optional<std::string> prompt_bundle_ref;
  std::string eval_suite_ref;
  std::string fallback_mode;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SkillMatchQuery {
  std::string intent_text;
  std::vector<std::string> tags;
  std::optional<std::string> profile_id;
};

struct SkillMatchResult {
  bool matched = false;
  std::optional<SkillSpecAsset> asset;
  std::string reason_code;
  std::vector<std::string> matched_terms;
  std::size_t score = 0U;
};

struct SkillRegistrySnapshot {
  std::map<std::string, std::vector<SkillSpecAsset>> assets_by_source;
  std::vector<SkillSpecAsset> assets;
  std::uint64_t revision = 0U;
};

class SkillRegistry {
 public:
  SkillRegistry() = default;

  [[nodiscard]] std::shared_ptr<const SkillRegistrySnapshot> snapshot() const;
  [[nodiscard]] bool register_asset(const SkillSpecAsset& asset);
  [[nodiscard]] SkillMatchResult match_intent(const SkillMatchQuery& query) const;
  [[nodiscard]] bool revoke_source(std::string_view source_key);
  [[nodiscard]] std::vector<SkillSpecAsset> list_assets() const;

 private:
  [[nodiscard]] static bool upsert_asset(
      SkillRegistrySnapshot& snapshot,
      const SkillSpecAsset& asset);
  static void rebuild_flat_view(SkillRegistrySnapshot& snapshot);
  void publish_snapshot(SkillRegistrySnapshot next_snapshot);

  mutable std::mutex write_mutex_;
  std::shared_ptr<const SkillRegistrySnapshot> snapshot_ =
      std::make_shared<const SkillRegistrySnapshot>();
};

}  // namespace dasall::tools::skills
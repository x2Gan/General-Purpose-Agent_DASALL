#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::cognition::validation {

struct EnumConstraint {
  std::string field_path;
  std::vector<std::string> allowed_values;
};

struct NumericConstraint {
  std::string field_path;
  std::optional<double> min_value;
  std::optional<double> max_value;
};

struct ListConstraint {
  std::string field_path;
  std::size_t min_items = 0U;
  std::optional<std::size_t> max_items;
};

enum class UnknownFieldPolicy : std::uint8_t {
  Reject = 0,
  AllowRegisteredExtensions = 1,
};

struct StageSchemaSpec {
  std::string stage_name;
  std::string schema_version;
  std::vector<std::string> known_top_level_fields;
  std::vector<std::string> required_fields;
  std::vector<EnumConstraint> enum_constraints;
  std::vector<NumericConstraint> numeric_bounds;
  std::vector<ListConstraint> list_constraints;
  std::vector<std::string> stage_specific_invariants;
  UnknownFieldPolicy unknown_field_policy = UnknownFieldPolicy::Reject;
  std::vector<std::string> allowed_extension_prefixes;
};

[[nodiscard]] const StageSchemaSpec& schema_for_planning_plan();
[[nodiscard]] const StageSchemaSpec& schema_for_perception_result();
[[nodiscard]] const StageSchemaSpec& schema_for_execution_action_decision();
[[nodiscard]] const StageSchemaSpec& schema_for_reflection_decision();
[[nodiscard]] const StageSchemaSpec& schema_for_response_envelope();

}  // namespace dasall::cognition::validation
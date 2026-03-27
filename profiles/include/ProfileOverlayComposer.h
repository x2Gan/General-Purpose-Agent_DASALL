#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ProfileError.h"
#include "RuntimePolicySnapshot.h"

namespace dasall::profiles {

enum class ProfileOverrideLayer {
  Deployment,
  Runtime,
};

enum class ProfileOverrideSourceKind {
  DeploymentBundle,
  SiteBundle,
  DeviceBundle,
  RuntimeCommand,
  DiagnosticSession,
  ExternalStoreSnapshot,
};

enum class ProfileOverrideTargetScope {
  Fleet,
  Site,
  Device,
  Process,
};

enum class ProfileOverridePatchOp {
  Replace,
  Remove,
};

struct ProfileOverridePatch {
  std::string path;
  ProfileOverridePatchOp op = ProfileOverridePatchOp::Replace;
  std::string value;

  [[nodiscard]] bool has_consistent_values() const {
    return !path.empty() && !(op == ProfileOverridePatchOp::Replace && value.empty());
  }
};

struct ProfileOverrideInput {
  ProfileOverrideLayer layer = ProfileOverrideLayer::Deployment;
  std::string override_id;
  ProfileOverrideSourceKind source_kind = ProfileOverrideSourceKind::DeploymentBundle;
  std::string source_id;
  std::string issued_by;
  ProfileOverrideTargetScope target_scope = ProfileOverrideTargetScope::Fleet;
  std::uint64_t base_version = 0U;
  std::optional<std::uint64_t> expires_at_epoch_ms;
  std::string reason_code;
  std::vector<ProfileOverridePatch> patches;

  [[nodiscard]] bool has_consistent_values() const;
};

struct ProfileOverlayComposeResult {
  std::shared_ptr<const RuntimePolicySnapshot> snapshot;
  std::vector<std::string> rejected_paths;
  std::optional<ProfileErrorCode> error_code;

  [[nodiscard]] bool ok() const {
    return static_cast<bool>(snapshot) && !error_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!ok()) {
      return !snapshot && error_code.has_value();
    }

    return rejected_paths.empty() && snapshot->has_consistent_values();
  }
};

class ProfileOverlayComposer final {
 public:
  [[nodiscard]] ProfileOverlayComposeResult compose(
      const RuntimePolicySnapshot& base,
      const std::optional<ProfileOverrideInput>& deployment_override,
      const std::optional<ProfileOverrideInput>& runtime_override) const;
};

}  // namespace dasall::profiles
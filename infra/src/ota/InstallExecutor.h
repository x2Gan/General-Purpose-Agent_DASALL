#pragma once

#include <string>
#include <string_view>

#include "ota/IInstallExecutor.h"

namespace dasall::infra::ota {

struct ArtifactWriteResult {
  bool written = false;
  std::string written_target;
  std::string checksum;
  std::string install_ts;
  std::string installer_version;
  std::string detail;

  [[nodiscard]] bool is_valid() const {
    return written && !written_target.empty() && !checksum.empty() &&
           !install_ts.empty() && !installer_version.empty();
  }
};

class IArtifactWriter {
 public:
  virtual ~IArtifactWriter() = default;

  [[nodiscard]] virtual ArtifactWriteResult write_repo_bound(
      const ArtifactDescriptor& artifact_descriptor,
      std::string_view target) const = 0;

  [[nodiscard]] virtual ArtifactWriteResult write_slot_bound(
      const ArtifactDescriptor& artifact_descriptor,
      std::string_view target) const = 0;
};

struct CleanupResult {
  bool cleaned = false;
  std::string detail;
};

class IInstallCleanupHandler {
 public:
  virtual ~IInstallCleanupHandler() = default;

  [[nodiscard]] virtual CleanupResult cleanup_failed_stage(
      const ArtifactDescriptor& artifact_descriptor,
      std::string_view target) const = 0;
};

class IPlanActivationAdapter {
 public:
  virtual ~IPlanActivationAdapter() = default;

  [[nodiscard]] virtual BootSwitchResult activate(
      const SlotPlan& slot_plan) const = 0;
};

class IInstallRevertAdapter {
 public:
  virtual ~IInstallRevertAdapter() = default;

  [[nodiscard]] virtual RollbackResult revert(
      const RollbackToken& rollback_token) const = 0;
};

class InstallExecutor final : public IInstallExecutor {
 public:
  struct Dependencies {
    const IArtifactWriter* artifact_writer = nullptr;
    const IInstallCleanupHandler* cleanup_handler = nullptr;
    const IPlanActivationAdapter* activation_adapter = nullptr;
    const IInstallRevertAdapter* revert_adapter = nullptr;
  };

  explicit InstallExecutor(Dependencies dependencies);

  [[nodiscard]] StageArtifactResult stage_artifact(
      const ArtifactDescriptor& artifact_descriptor,
      std::string_view target) override;

  [[nodiscard]] BootSwitchResult activate_plan(
      const SlotPlan& slot_plan) override;

  [[nodiscard]] RollbackResult revert_install(
      const RollbackToken& rollback_token) override;

 private:
  Dependencies dependencies_;
};

}  // namespace dasall::infra::ota
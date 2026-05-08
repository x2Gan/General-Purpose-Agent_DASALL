#pragma once

#include <string>
#include <vector>

#include "config/ConfigCommandTypes.h"

namespace dasall::apps::cli::config {

struct InstallStateFacts {
  bool systemd_available = true;
  bool install_payload_complete = true;
  bool defaults_file_present = false;
  bool profile_id_present = false;
  bool daemon_config_file_present = false;
  bool daemon_config_valid = false;
  bool secret_requirements_satisfied = true;
  bool service_installed = false;
  bool service_enabled = false;
  bool service_running = false;
  bool daemon_ping_ok = false;
  bool daemon_readiness_ok = false;
};

struct InstallStateProbeResult {
  InstallState state = InstallState::FreshInstall;
  InstallStateFacts facts;
  std::vector<std::string> gaps;

  [[nodiscard]] bool has_gap(std::string_view gap) const;
};

class InstallStateProbe {
 public:
  [[nodiscard]] InstallStateProbeResult probe(const InstallStateFacts& facts) const;
};

}  // namespace dasall::apps::cli::config
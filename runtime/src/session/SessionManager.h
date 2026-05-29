#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "session/ISessionManager.h"

namespace dasall::infra::logging {
class ILogger;
}

namespace dasall::runtime {

class SessionManager final : public ISessionManager {
 public:
  SessionManager() = default;

    void set_logger(
            std::shared_ptr<infra::logging::ILogger> logger,
            std::optional<std::string> runtime_instance_id = std::nullopt);
    void set_durable_state_root(const std::optional<std::string>& state_root);
    void seed_for_test(const SessionSnapshot& session_snapshot);

  [[nodiscard]] SessionLoadResult load_session(
      const SessionLoadRequest& request) const override;
  [[nodiscard]] PrepareTurnResult prepare_turn(
      const PrepareTurnRequest& request) const override;
  [[nodiscard]] SessionPersistResult persist_turn(
      const SessionPersistRequest& request) override;
  [[nodiscard]] SessionPersistResult bind_checkpoint_ref(
      const BindCheckpointRefRequest& request) override;
  [[nodiscard]] ResumeSeedResult build_resume_seed(
      const BuildResumeSeedRequest& request) const override;

 private:
  mutable std::mutex session_mutex_;
    mutable std::unordered_map<std::string, SessionSnapshot> stored_snapshots_;
    std::optional<std::string> durable_state_root_;
    std::shared_ptr<infra::logging::ILogger> logger_;
    std::optional<std::string> runtime_instance_id_;
};

}  // namespace dasall::runtime
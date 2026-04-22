#pragma once

#include <mutex>
#include <optional>

#include "session/ISessionManager.h"

namespace dasall::runtime {

class SessionManager final : public ISessionManager {
 public:
  SessionManager() = default;

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
  std::optional<SessionSnapshot> stored_snapshot_;
};

}  // namespace dasall::runtime
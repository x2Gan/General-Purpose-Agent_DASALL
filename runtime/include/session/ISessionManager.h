#pragma once

#include "session/SessionTypes.h"

namespace dasall::runtime {

class ISessionManager {
 public:
  virtual ~ISessionManager() = default;

  [[nodiscard]] virtual SessionLoadResult load_session(
      const SessionLoadRequest& request) const = 0;

  [[nodiscard]] virtual PrepareTurnResult prepare_turn(
      const PrepareTurnRequest& request) const = 0;

  [[nodiscard]] virtual SessionPersistResult persist_turn(
      const SessionPersistRequest& request) = 0;

  [[nodiscard]] virtual SessionPersistResult bind_checkpoint_ref(
      const BindCheckpointRefRequest& request) = 0;

  [[nodiscard]] virtual ResumeSeedResult build_resume_seed(
      const BuildResumeSeedRequest& request) const = 0;
};

}  // namespace dasall::runtime
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "IIPC.h"
#include "ipc/TuiIpcController.h"

namespace dasall::tui::ipc::test {

class ScopedIpcOverride {
 public:
  explicit ScopedIpcOverride(std::shared_ptr<dasall::platform::IIPC> ipc);
  ~ScopedIpcOverride();

  ScopedIpcOverride(const ScopedIpcOverride&) = delete;
  ScopedIpcOverride& operator=(const ScopedIpcOverride&) = delete;

 private:
  std::shared_ptr<dasall::platform::IIPC> previous_;
};

[[nodiscard]] std::string encode_response_envelope_for_test(
    const TuiIpcResponseEnvelope& envelope);

[[nodiscard]] std::optional<TuiIpcRequestEnvelope>
decode_request_envelope_for_test(std::string_view payload);

}  // namespace dasall::tui::ipc::test
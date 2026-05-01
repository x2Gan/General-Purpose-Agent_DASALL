#include "DaemonListenerHost.h"

#include <string>
#include <utility>

#include "DaemonSocketPolicy.h"
#include "PlatformError.h"

namespace dasall::apps::daemon {

namespace {

[[nodiscard]] dasall::platform::PlatformError make_error(
    dasall::platform::PlatformErrorCode code,
    dasall::platform::PlatformErrorCategory category,
    std::string detail) {
  return dasall::platform::PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = false,
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

}  // namespace

DaemonListenerHost::DaemonListenerHost(
    std::shared_ptr<dasall::platform::IIPC> ipc)
    : ipc_(std::move(ipc)) {}

dasall::platform::PlatformResult<bool> DaemonListenerHost::bind(
    const dasall::platform::IpcEndpoint& endpoint) {
  const DaemonSocketPolicy socket_policy =
      DaemonSocketPolicy::for_current_process();

  if (!ipc_) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "ipc provider is required for listener host binding"));
  }

  if (!endpoint.has_consistent_values()) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "listener endpoint is invalid"));
  }

  const auto preflight = preflight_bind_endpoint(endpoint, socket_policy);
  if (!preflight.ok()) {
    return dasall::platform::PlatformResult<bool>::failure(*preflight.error);
  }

  auto listener_result = ipc_->listen(endpoint, listen_options_);
  if ((!listener_result.ok() || !listener_result.value.has_value()) &&
      listener_result.error.has_value() &&
      listener_result.error->code ==
          dasall::platform::PlatformErrorCode::AddressInUse) {
    const auto cleanup_result =
        try_cleanup_stale_socket(endpoint.socket_path, socket_policy);
    if (!cleanup_result.ok()) {
      return dasall::platform::PlatformResult<bool>::failure(
          *cleanup_result.error);
    }

    if (cleanup_result.value.value_or(false)) {
      listener_result = ipc_->listen(endpoint, listen_options_);
    }
  }

  if (!listener_result.ok() || !listener_result.value.has_value()) {
    if (listener_result.error.has_value()) {
      return dasall::platform::PlatformResult<bool>::failure(
          *listener_result.error);
    }

    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InternalFailure,
        dasall::platform::PlatformErrorCategory::Internal,
        "listener bind failed without platform error"));
  }

  listener_ = *listener_result.value;
  closed_ = false;
  return dasall::platform::PlatformResult<bool>::success(true);
}

void DaemonListenerHost::set_listen_options(
    const dasall::platform::ListenOptions& options) {
  if (!options.has_consistent_values()) {
    return;
  }

  listen_options_ = options;
}

void DaemonListenerHost::set_connection_handler(ConnectionHandler handler) {
  connection_handler_ = std::move(handler);
}

dasall::platform::PlatformResult<bool> DaemonListenerHost::accept_loop(
    const std::atomic<bool>& stop_requested,
    std::int32_t accept_deadline_ms) {
  if (!ipc_) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "ipc provider is required for listener accept loop"));
  }

  if (!listener_.has_value() || closed_) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::NotFound,
        dasall::platform::PlatformErrorCategory::Resource,
        "listener host is not bound"));
  }

  if (!connection_handler_) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "connection handler is required for listener accept loop"));
  }

  if (accept_deadline_ms <= 0) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "accept deadline must be positive"));
  }

  while (!stop_requested.load() && !closed_) {
    const auto accept_result = ipc_->accept(*listener_, accept_deadline_ms);
    if (!accept_result.ok()) {
      if (closed_ || stop_requested.load()) {
        break;
      }

      if (accept_result.error.has_value() &&
          accept_result.error->code ==
              dasall::platform::PlatformErrorCode::Timeout) {
        continue;
      }

      if (accept_result.error.has_value()) {
        return dasall::platform::PlatformResult<bool>::failure(
            *accept_result.error);
      }

      return dasall::platform::PlatformResult<bool>::failure(make_error(
          dasall::platform::PlatformErrorCode::InternalFailure,
          dasall::platform::PlatformErrorCategory::Internal,
          "listener accept failed without platform error"));
    }

    if (!accept_result.value.has_value()) {
      continue;
    }

    const auto& channel = *accept_result.value;
    (void)connection_handler_(channel);

    const auto close_result = ipc_->close(channel);
    if (!close_result.ok()) {
      if (close_result.error.has_value()) {
        return dasall::platform::PlatformResult<bool>::failure(
            *close_result.error);
      }

      return dasall::platform::PlatformResult<bool>::failure(make_error(
          dasall::platform::PlatformErrorCode::InternalFailure,
          dasall::platform::PlatformErrorCategory::Internal,
          "listener channel close failed without platform error"));
    }
  }

  return dasall::platform::PlatformResult<bool>::success(true);
}

dasall::platform::PlatformResult<bool> DaemonListenerHost::close() {
  if (!listener_.has_value()) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::NotFound,
        dasall::platform::PlatformErrorCategory::Resource,
        "listener host is not bound"));
  }

  closed_ = true;
  if (ipc_) {
    (void)ipc_->close(
        dasall::platform::IpcChannelHandle{.native_fd = listener_->native_fd});
  }
  listener_.reset();
  return dasall::platform::PlatformResult<bool>::success(true);
}

bool DaemonListenerHost::is_bound() const {
  return listener_.has_value() && !closed_;
}

}  // namespace dasall::apps::daemon
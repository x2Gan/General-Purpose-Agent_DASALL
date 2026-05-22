#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "data/TuiProjectionTypes.h"

namespace dasall::tui::data {

struct TuiDataSourceIssue {
  std::string reason_domain;
  std::string reason_code;
  std::string message;
  bool retryable = false;
  std::optional<std::string> error_ref;
  std::vector<std::pair<std::string, std::string>> metadata;

  [[nodiscard]] bool has_reason() const {
    return !reason_domain.empty() && !reason_code.empty();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!has_reason()) {
      return message.empty() && !retryable && !error_ref.has_value() && metadata.empty();
    }

    return true;
  }
};

struct TuiOpenSessionRequest {
  std::optional<std::string> profile_id;
  std::optional<std::string> startup_mode_hint;
  std::string request_id;
  std::string trace_id;
};

struct TuiOpenSessionResult {
  std::optional<TuiSessionView> session;
  std::optional<TuiDataSourceIssue> issue;

  [[nodiscard]] bool ok() const {
    return session.has_value() && !issue.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    return session.has_value() != issue.has_value();
  }
};

struct TuiSubmitTurnRequest {
  std::string session_id;
  std::string user_input;
  NextTurnPreference next_preference;
  std::string request_id;
  std::string trace_id;
};

struct TuiSubmitTurnResult {
  std::optional<TuiTurnReceipt> receipt;
  std::optional<TuiDataSourceIssue> issue;

  [[nodiscard]] bool ok() const {
    return receipt.has_value() && !issue.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    return receipt.has_value() != issue.has_value();
  }
};

struct TuiPollEventsRequest {
  std::string session_id;
  std::optional<std::string> event_cursor;
  std::string request_id;
  std::string trace_id;
};

struct TuiPollEventsResult {
  std::vector<TuiEventProjection> events;
  std::optional<std::string> next_cursor;
  std::optional<TuiDataSourceIssue> issue;

  [[nodiscard]] bool ok() const {
    return !issue.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    return !issue.has_value() || (events.empty() && !next_cursor.has_value());
  }
};

struct TuiRouteCatalogRequest {
  std::optional<std::string> session_id;
  std::optional<std::string> profile_id;
  std::optional<std::string> selector_mode;
  std::string request_id;
  std::string trace_id;
};

struct TuiRouteCatalogResult {
  std::optional<TuiRouteCatalogView> route_catalog;
  std::optional<TuiDataSourceIssue> issue;

  [[nodiscard]] bool ok() const {
    return route_catalog.has_value() && !issue.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    return route_catalog.has_value() != issue.has_value();
  }
};

struct TuiCloseSessionRequest {
  std::string session_id;
  std::string close_reason;
  std::string request_id;
  std::string trace_id;
};

struct TuiCloseSessionResult {
  bool closed = false;
  std::optional<TuiDataSourceIssue> issue;

  [[nodiscard]] bool ok() const {
    return closed && !issue.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    return closed != issue.has_value();
  }
};

class ITuiDataSource {
 public:
  virtual ~ITuiDataSource() = default;

  virtual TuiOpenSessionResult open_session(const TuiOpenSessionRequest& request) = 0;
  virtual TuiSubmitTurnResult submit_turn(const TuiSubmitTurnRequest& request) = 0;
  virtual TuiPollEventsResult poll_events(const TuiPollEventsRequest& request) = 0;
  virtual TuiRouteCatalogResult route_catalog(const TuiRouteCatalogRequest& request) = 0;
  virtual TuiCloseSessionResult close_session(const TuiCloseSessionRequest& request) = 0;
};

}  // namespace dasall::tui::data
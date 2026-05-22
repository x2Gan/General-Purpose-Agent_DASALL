#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "data/FakeScenarioCatalog.h"

namespace dasall::tui::data {

class FakeTuiDataSource final : public ITuiDataSource {
 public:
  explicit FakeTuiDataSource(std::string_view scenario_id = "golden_ready");
  explicit FakeTuiDataSource(FakeScenario scenario);

  [[nodiscard]] std::string_view scenario_id() const;
  [[nodiscard]] bool has_loaded_scenario() const;

  TuiOpenSessionResult open_session(const TuiOpenSessionRequest& request) override;
  TuiSubmitTurnResult submit_turn(const TuiSubmitTurnRequest& request) override;
  TuiPollEventsResult poll_events(const TuiPollEventsRequest& request) override;
  TuiRouteCatalogResult route_catalog(const TuiRouteCatalogRequest& request) override;
  TuiCloseSessionResult close_session(const TuiCloseSessionRequest& request) override;

 private:
  [[nodiscard]] std::optional<TuiDataSourceIssue> validate_loaded_scenario() const;
  [[nodiscard]] std::optional<TuiDataSourceIssue> validate_session_id(std::string_view session_id) const;

  std::string requested_scenario_id_;
  std::optional<FakeScenario> scenario_;
  std::optional<TuiDataSourceIssue> scenario_issue_;
  std::optional<TuiSessionView> current_session_;
  std::optional<std::string> last_event_cursor_;
  std::size_t next_event_batch_index_ = 0;
  bool session_open_ = false;
};

}  // namespace dasall::tui::data
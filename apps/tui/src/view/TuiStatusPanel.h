#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "data/TuiProjectionTypes.h"

namespace dasall::tui::view {

struct TuiStatusPanelLine {
  std::string text;
  bool degraded = false;
};

struct TuiStatusPanelRenderResult {
  std::vector<TuiStatusPanelLine> lines;
  std::string stage_badge;
  std::string health_summary;
  std::string decision_summary;
  bool narrow_layout = false;
  bool degraded = false;
};

class TuiStatusPanel {
 public:
  explicit TuiStatusPanel(data::TuiStatusProjection status = {});

  void set_status(data::TuiStatusProjection status);

  [[nodiscard]] const data::TuiStatusProjection& status() const noexcept;

  [[nodiscard]] TuiStatusPanelRenderResult render_status_panel(
      std::size_t panel_width = 48) const;

  [[nodiscard]] std::string format_stage_badge() const;

  [[nodiscard]] std::string format_health_summary() const;

 private:
  [[nodiscard]] std::string format_decision_summary() const;

  data::TuiStatusProjection status_;
};

}  // namespace dasall::tui::view
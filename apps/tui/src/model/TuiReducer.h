#pragma once

#include "model/TuiScreenModel.h"

namespace dasall::tui::model {

TuiScreenModel reduce(TuiScreenModel current, TuiAction action);

}  // namespace dasall::tui::model
# TUI target wiring is owned by TUI-TODO-007. This file only freezes
# the FTXUI private-link policy decided by TUI-TODO-005.

function(dasall_tui_apply_ftxui_private_dependency target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "dasall_tui_apply_ftxui_private_dependency requires an existing target")
  endif()

  if(NOT TARGET ftxui::component OR NOT TARGET ftxui::dom OR NOT TARGET ftxui::screen)
    message(FATAL_ERROR
      "FTXUI dependency is not resolved. Enable DASALL_ENABLE_TUI_FTXUI before linking apps/tui targets."
    )
  endif()

  target_link_libraries(${target_name}
    PRIVATE
      ftxui::component
      ftxui::dom
      ftxui::screen
  )
endfunction()

add_library(dasall_tui_prototype_core STATIC
  src/app/TuiApp.cpp
  src/data/FakeTuiDataSource.cpp
  src/model/TuiReducer.cpp
  src/terminal/FtxuiRendererAdapter.cpp
  src/terminal/TuiTerminalCapabilityProbe.cpp
  src/view/TuiComposer.cpp
  src/view/TuiModelSelector.cpp
  src/view/TuiStatusPanel.cpp
  src/view/TuiTranscriptView.cpp
)

add_executable(dasall_tui_prototype EXCLUDE_FROM_ALL
  src/main.cpp
)

add_executable(dasall-tui
  src/main.cpp
  src/data/DaemonTuiDataSource.cpp
  src/ipc/TuiIpcController.cpp
)

add_executable(dasall_tui_manual_terminal EXCLUDE_FROM_ALL
  src/manual_terminal_main.cpp
)

target_include_directories(dasall_tui_prototype_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(dasall_tui_prototype PRIVATE cxx_std_20)
target_compile_features(dasall-tui PRIVATE cxx_std_20)
target_compile_features(dasall_tui_manual_terminal PRIVATE cxx_std_20)
target_compile_features(dasall_tui_prototype_core PRIVATE cxx_std_20)

target_compile_definitions(dasall-tui PRIVATE DASALL_TUI_FORMAL_ENTRYPOINT=1)

if(TARGET ftxui::component AND TARGET ftxui::dom AND TARGET ftxui::screen)
  target_compile_definitions(dasall_tui_prototype PRIVATE DASALL_TUI_PROTOTYPE_HAS_FTXUI=1)
  target_compile_definitions(dasall_tui_prototype_core PRIVATE DASALL_TUI_RENDERER_USE_FTXUI=1)
  dasall_tui_apply_ftxui_private_dependency(dasall_tui_prototype_core)
else()
  target_compile_definitions(dasall_tui_prototype PRIVATE DASALL_TUI_PROTOTYPE_HAS_FTXUI=0)
  target_compile_definitions(dasall_tui_prototype_core PRIVATE DASALL_TUI_RENDERER_USE_FTXUI=0)
endif()

target_link_libraries(dasall_tui_prototype
  PRIVATE
    dasall_tui_prototype_core
)

target_link_libraries(dasall-tui
  PRIVATE
    dasall_tui_prototype_core
    dasall_platform
)

target_link_libraries(dasall_tui_manual_terminal
  PRIVATE
    dasall_tui_prototype_core
)

set_target_properties(dasall-tui PROPERTIES
  OUTPUT_NAME dasall
)

install(TARGETS dasall-tui
  RUNTIME DESTINATION ${DASALL_INSTALL_BINDIR}
)

dasall_apply_common_options(dasall_tui_prototype_core)
dasall_apply_common_options(dasall_tui_prototype)
dasall_apply_common_options(dasall-tui)
dasall_apply_common_options(dasall_tui_manual_terminal)
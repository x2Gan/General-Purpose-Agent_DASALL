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

add_executable(dasall_tui_prototype EXCLUDE_FROM_ALL
  src/main.cpp
)

target_compile_features(dasall_tui_prototype PRIVATE cxx_std_20)

if(TARGET ftxui::component AND TARGET ftxui::dom AND TARGET ftxui::screen)
  target_compile_definitions(dasall_tui_prototype PRIVATE DASALL_TUI_PROTOTYPE_HAS_FTXUI=1)
  dasall_tui_apply_ftxui_private_dependency(dasall_tui_prototype)
else()
  target_compile_definitions(dasall_tui_prototype PRIVATE DASALL_TUI_PROTOTYPE_HAS_FTXUI=0)
endif()

dasall_apply_common_options(dasall_tui_prototype)
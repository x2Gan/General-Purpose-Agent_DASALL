set(DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_tui_unit_topology_smoke_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiUnitTopologySmokeTest.cpp
)

target_link_libraries(dasall_tui_unit_topology_smoke_unit_test
  PRIVATE
    dasall_test_support
)

target_compile_definitions(dasall_tui_unit_topology_smoke_unit_test
  PRIVATE
    DASALL_TUI_UNIT_TEST_DIR="${CMAKE_SOURCE_DIR}/tests/unit/tui"
    DASALL_TUI_UNIT_TOP_LEVEL_CMAKE="${CMAKE_SOURCE_DIR}/tests/unit/CMakeLists.txt"
    DASALL_TUI_UNIT_LOCAL_CMAKE="${CMAKE_SOURCE_DIR}/tests/unit/tui/CMakeLists.txt"
    DASALL_TUI_GOLDEN_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/tui/golden"
)

dasall_apply_common_options(dasall_tui_unit_topology_smoke_unit_test)

add_test(NAME TuiScreenModelTest COMMAND dasall_tui_unit_topology_smoke_unit_test)
set_tests_properties(TuiScreenModelTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiReducerTransitionTest COMMAND dasall_tui_unit_topology_smoke_unit_test)
set_tests_properties(TuiReducerTransitionTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiComposerTest COMMAND dasall_tui_unit_topology_smoke_unit_test)
set_tests_properties(TuiComposerTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiMainLayoutSnapshotTest COMMAND dasall_tui_unit_topology_smoke_unit_test)
set_tests_properties(TuiMainLayoutSnapshotTest PROPERTIES LABELS "unit;snapshot;tui")

list(APPEND DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_tui_unit_topology_smoke_unit_test
)

set(DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS
  ${DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
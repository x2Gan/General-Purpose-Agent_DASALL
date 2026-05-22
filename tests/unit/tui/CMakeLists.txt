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

add_executable(dasall_tui_projection_types_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiProjectionTypesTest.cpp
)

add_executable(dasall_tui_data_source_contract_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiDataSourceContractTest.cpp
)

add_executable(dasall_tui_fake_scenario_catalog_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiFakeScenarioCatalogTest.cpp
)

add_executable(dasall_tui_fake_data_source_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/FakeTuiDataSourceTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/data/FakeTuiDataSource.cpp
)

add_executable(dasall_tui_screen_model_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiScreenModelTest.cpp
)

add_executable(dasall_tui_reducer_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiReducerTransitionTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/model/TuiReducer.cpp
)

add_executable(dasall_tui_slash_command_parser_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiSlashCommandParserTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/command/TuiSlashCommandParser.cpp
)

add_executable(dasall_tui_composer_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiComposerTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiComposer.cpp
)

add_executable(dasall_tui_composer_history_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiComposerHistoryTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiComposer.cpp
)

add_executable(dasall_tui_model_selector_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiModelSelectorTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiModelSelector.cpp
)

add_executable(dasall_tui_transcript_view_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiTranscriptViewTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiTranscriptView.cpp
)

add_executable(dasall_tui_status_panel_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiStatusPanelTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiStatusPanel.cpp
)

add_executable(dasall_tui_route_catalog_filter_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiRouteCatalogFilterTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiModelSelector.cpp
)

target_include_directories(dasall_tui_projection_types_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_data_source_contract_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_fake_scenario_catalog_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_fake_data_source_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_screen_model_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_reducer_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_slash_command_parser_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_composer_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_composer_history_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_model_selector_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_transcript_view_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_status_panel_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_route_catalog_filter_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_link_libraries(dasall_tui_projection_types_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_data_source_contract_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_fake_scenario_catalog_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_fake_data_source_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_screen_model_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_reducer_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_slash_command_parser_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_composer_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_composer_history_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_model_selector_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_transcript_view_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_status_panel_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_route_catalog_filter_unit_test
  PRIVATE
    dasall_test_support
)

target_compile_definitions(dasall_tui_projection_types_unit_test
  PRIVATE
    DASALL_TUI_PROJECTION_TYPES_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/data/TuiProjectionTypes.h"
)

target_compile_definitions(dasall_tui_data_source_contract_unit_test
  PRIVATE
    DASALL_TUI_DATA_SOURCE_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/data/ITuiDataSource.h"
)

target_compile_definitions(dasall_tui_fake_scenario_catalog_unit_test
  PRIVATE
    DASALL_TUI_FAKE_SCENARIO_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/data/FakeScenarioCatalog.h"
)

target_compile_definitions(dasall_tui_fake_data_source_unit_test
  PRIVATE
    DASALL_TUI_FAKE_DATA_SOURCE_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/data/FakeTuiDataSource.h"
    DASALL_TUI_FAKE_DATA_SOURCE_IMPL="${CMAKE_SOURCE_DIR}/apps/tui/src/data/FakeTuiDataSource.cpp"
)

target_compile_definitions(dasall_tui_screen_model_unit_test
  PRIVATE
    DASALL_TUI_ACTION_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/model/TuiAction.h"
    DASALL_TUI_SCREEN_MODEL_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/model/TuiScreenModel.h"
)

target_compile_definitions(dasall_tui_reducer_unit_test
  PRIVATE
    DASALL_TUI_REDUCER_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/model/TuiReducer.h"
    DASALL_TUI_REDUCER_IMPL="${CMAKE_SOURCE_DIR}/apps/tui/src/model/TuiReducer.cpp"
)

target_compile_definitions(dasall_tui_slash_command_parser_unit_test
  PRIVATE
    DASALL_TUI_SLASH_COMMAND_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/command/TuiSlashCommandParser.h"
    DASALL_TUI_SLASH_COMMAND_IMPL="${CMAKE_SOURCE_DIR}/apps/tui/src/command/TuiSlashCommandParser.cpp"
)

target_compile_definitions(dasall_tui_composer_unit_test
  PRIVATE
    DASALL_TUI_COMPOSER_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiComposer.h"
    DASALL_TUI_COMPOSER_IMPL="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiComposer.cpp"
    DASALL_TUI_INPUT_HISTORY_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiInputHistory.h"
)

target_compile_definitions(dasall_tui_composer_history_unit_test
  PRIVATE
    DASALL_TUI_INPUT_HISTORY_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiInputHistory.h"
)

target_compile_definitions(dasall_tui_model_selector_unit_test
  PRIVATE
    DASALL_TUI_MODEL_SELECTOR_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiModelSelector.h"
    DASALL_TUI_MODEL_SELECTOR_IMPL="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiModelSelector.cpp"
)

target_compile_definitions(dasall_tui_transcript_view_unit_test
  PRIVATE
    DASALL_TUI_TRANSCRIPT_VIEW_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiTranscriptView.h"
    DASALL_TUI_TRANSCRIPT_VIEW_IMPL="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiTranscriptView.cpp"
)

target_compile_definitions(dasall_tui_status_panel_unit_test
  PRIVATE
    DASALL_TUI_STATUS_PANEL_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiStatusPanel.h"
    DASALL_TUI_STATUS_PANEL_IMPL="${CMAKE_SOURCE_DIR}/apps/tui/src/view/TuiStatusPanel.cpp"
)

dasall_apply_common_options(dasall_tui_projection_types_unit_test)
dasall_apply_common_options(dasall_tui_data_source_contract_unit_test)
dasall_apply_common_options(dasall_tui_fake_scenario_catalog_unit_test)
dasall_apply_common_options(dasall_tui_fake_data_source_unit_test)
dasall_apply_common_options(dasall_tui_screen_model_unit_test)
dasall_apply_common_options(dasall_tui_reducer_unit_test)
dasall_apply_common_options(dasall_tui_slash_command_parser_unit_test)
dasall_apply_common_options(dasall_tui_composer_unit_test)
dasall_apply_common_options(dasall_tui_composer_history_unit_test)
dasall_apply_common_options(dasall_tui_model_selector_unit_test)
dasall_apply_common_options(dasall_tui_transcript_view_unit_test)
dasall_apply_common_options(dasall_tui_status_panel_unit_test)
dasall_apply_common_options(dasall_tui_route_catalog_filter_unit_test)

add_test(NAME TuiProjectionTypesTest COMMAND dasall_tui_projection_types_unit_test)
set_tests_properties(TuiProjectionTypesTest PROPERTIES LABELS "unit;tui")

add_test(NAME ITuiDataSourceContractTest COMMAND dasall_tui_data_source_contract_unit_test)
set_tests_properties(ITuiDataSourceContractTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiFakeScenarioCatalogTest COMMAND dasall_tui_fake_scenario_catalog_unit_test)
set_tests_properties(TuiFakeScenarioCatalogTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiFakeDataSourceTest COMMAND dasall_tui_fake_data_source_unit_test)
set_tests_properties(TuiFakeDataSourceTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiScreenModelTest COMMAND dasall_tui_screen_model_unit_test)
set_tests_properties(TuiScreenModelTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiReducerTransitionTest COMMAND dasall_tui_reducer_unit_test)
set_tests_properties(TuiReducerTransitionTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiSlashCommandParserTest COMMAND dasall_tui_slash_command_parser_unit_test)
set_tests_properties(TuiSlashCommandParserTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiComposerTest COMMAND dasall_tui_composer_unit_test)
set_tests_properties(TuiComposerTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiComposerHistoryTest COMMAND dasall_tui_composer_history_unit_test)
set_tests_properties(TuiComposerHistoryTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiModelSelectorTest COMMAND dasall_tui_model_selector_unit_test)
set_tests_properties(TuiModelSelectorTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiTranscriptViewTest COMMAND dasall_tui_transcript_view_unit_test)
set_tests_properties(TuiTranscriptViewTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiStatusPanelTest COMMAND dasall_tui_status_panel_unit_test)
set_tests_properties(TuiStatusPanelTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiRouteCatalogFilterTest COMMAND dasall_tui_route_catalog_filter_unit_test)
set_tests_properties(TuiRouteCatalogFilterTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiMainLayoutSnapshotTest COMMAND dasall_tui_unit_topology_smoke_unit_test)
set_tests_properties(TuiMainLayoutSnapshotTest PROPERTIES LABELS "unit;snapshot;tui")

list(APPEND DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_tui_data_source_contract_unit_test
  dasall_tui_fake_data_source_unit_test
  dasall_tui_fake_scenario_catalog_unit_test
  dasall_tui_projection_types_unit_test
  dasall_tui_reducer_unit_test
  dasall_tui_screen_model_unit_test
  dasall_tui_slash_command_parser_unit_test
  dasall_tui_composer_unit_test
  dasall_tui_composer_history_unit_test
  dasall_tui_model_selector_unit_test
  dasall_tui_transcript_view_unit_test
  dasall_tui_status_panel_unit_test
  dasall_tui_route_catalog_filter_unit_test
  dasall_tui_unit_topology_smoke_unit_test
)

set(DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS
  ${DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
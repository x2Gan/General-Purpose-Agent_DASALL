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

add_executable(dasall_tui_screen_model_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiScreenModelTest.cpp
)

add_executable(dasall_tui_reducer_unit_test
  ${CMAKE_CURRENT_SOURCE_DIR}/TuiReducerTransitionTest.cpp
  ${CMAKE_SOURCE_DIR}/apps/tui/src/model/TuiReducer.cpp
)

target_include_directories(dasall_tui_projection_types_unit_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/tui/src
)

target_include_directories(dasall_tui_data_source_contract_unit_test
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

target_link_libraries(dasall_tui_projection_types_unit_test
  PRIVATE
    dasall_test_support
)

target_link_libraries(dasall_tui_data_source_contract_unit_test
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

target_compile_definitions(dasall_tui_projection_types_unit_test
  PRIVATE
    DASALL_TUI_PROJECTION_TYPES_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/data/TuiProjectionTypes.h"
)

target_compile_definitions(dasall_tui_data_source_contract_unit_test
  PRIVATE
    DASALL_TUI_DATA_SOURCE_HEADER="${CMAKE_SOURCE_DIR}/apps/tui/src/data/ITuiDataSource.h"
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

dasall_apply_common_options(dasall_tui_projection_types_unit_test)
dasall_apply_common_options(dasall_tui_data_source_contract_unit_test)
dasall_apply_common_options(dasall_tui_screen_model_unit_test)
dasall_apply_common_options(dasall_tui_reducer_unit_test)

add_test(NAME TuiProjectionTypesTest COMMAND dasall_tui_projection_types_unit_test)
set_tests_properties(TuiProjectionTypesTest PROPERTIES LABELS "unit;tui")

add_test(NAME ITuiDataSourceContractTest COMMAND dasall_tui_data_source_contract_unit_test)
set_tests_properties(ITuiDataSourceContractTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiScreenModelTest COMMAND dasall_tui_screen_model_unit_test)
set_tests_properties(TuiScreenModelTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiReducerTransitionTest COMMAND dasall_tui_reducer_unit_test)
set_tests_properties(TuiReducerTransitionTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiComposerTest COMMAND dasall_tui_unit_topology_smoke_unit_test)
set_tests_properties(TuiComposerTest PROPERTIES LABELS "unit;tui")

add_test(NAME TuiMainLayoutSnapshotTest COMMAND dasall_tui_unit_topology_smoke_unit_test)
set_tests_properties(TuiMainLayoutSnapshotTest PROPERTIES LABELS "unit;snapshot;tui")

list(APPEND DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_tui_data_source_contract_unit_test
  dasall_tui_projection_types_unit_test
  dasall_tui_reducer_unit_test
  dasall_tui_screen_model_unit_test
  dasall_tui_unit_topology_smoke_unit_test
)

set(DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS
  ${DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
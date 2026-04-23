set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)

add_executable(dasall_access_interface_surface_unit_test
  AccessInterfaceSurfaceTest.cpp
)

target_link_libraries(dasall_access_interface_surface_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
)

dasall_apply_common_options(dasall_access_interface_surface_unit_test)

add_test(NAME AccessInterfaceSurfaceTest COMMAND dasall_access_interface_surface_unit_test)
set_tests_properties(AccessInterfaceSurfaceTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_interface_surface_unit_test
)

if(NOT DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "access unit registration requires at least one test target")
endif()

set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  ${DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)

add_executable(dasall_platform_init_config_unit_test
  PlatformInitConfigTest.cpp
)

target_link_libraries(dasall_platform_init_config_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_platform_init_config_unit_test)

add_test(NAME PlatformInitConfigTest COMMAND dasall_platform_init_config_unit_test)
set_tests_properties(PlatformInitConfigTest PROPERTIES LABELS "unit")
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

add_executable(dasall_linux_platform_capabilities_unit_test
  LinuxPlatformCapabilitiesTest.cpp
)

target_link_libraries(dasall_linux_platform_capabilities_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_linux_platform_capabilities_unit_test)

add_test(NAME LinuxPlatformCapabilitiesTest COMMAND dasall_linux_platform_capabilities_unit_test)
set_tests_properties(LinuxPlatformCapabilitiesTest PROPERTIES LABELS "unit")

add_executable(dasall_platform_error_mapping_unit_test
  PlatformErrorMappingTest.cpp
)

target_link_libraries(dasall_platform_error_mapping_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_platform_error_mapping_unit_test)

add_test(NAME PlatformErrorMappingTest COMMAND dasall_platform_error_mapping_unit_test)
set_tests_properties(PlatformErrorMappingTest PROPERTIES LABELS "unit")
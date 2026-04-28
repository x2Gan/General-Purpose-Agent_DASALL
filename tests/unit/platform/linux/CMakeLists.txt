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

add_executable(dasall_platform_interface_surface_unit_test
  InterfaceSurfaceTest.cpp
)

target_link_libraries(dasall_platform_interface_surface_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_platform_interface_surface_unit_test)

add_test(NAME InterfaceSurfaceTest COMMAND dasall_platform_interface_surface_unit_test)
set_tests_properties(InterfaceSurfaceTest PROPERTIES LABELS "unit")

add_executable(dasall_linux_platform_factory_unit_test
  LinuxPlatformFactoryTest.cpp
)

target_link_libraries(dasall_linux_platform_factory_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_linux_platform_factory_unit_test)

add_test(NAME LinuxPlatformFactoryTest COMMAND dasall_linux_platform_factory_unit_test)
set_tests_properties(LinuxPlatformFactoryTest PROPERTIES LABELS "unit")

add_executable(dasall_capability_registry_unit_test
  CapabilityRegistryTest.cpp
)

target_link_libraries(dasall_capability_registry_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_capability_registry_unit_test)

add_test(NAME CapabilityRegistryTest COMMAND dasall_capability_registry_unit_test)
set_tests_properties(CapabilityRegistryTest PROPERTIES LABELS "unit")

add_executable(dasall_posix_thread_provider_unit_test
  PosixThreadProviderTest.cpp
)

target_link_libraries(dasall_posix_thread_provider_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_posix_thread_provider_unit_test)

add_test(NAME PosixThreadProviderTest COMMAND dasall_posix_thread_provider_unit_test)
set_tests_properties(PosixThreadProviderTest PROPERTIES LABELS "unit")

add_executable(dasall_posix_timer_provider_unit_test
  PosixTimerProviderTest.cpp
)

target_link_libraries(dasall_posix_timer_provider_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_posix_timer_provider_unit_test)

add_test(NAME PosixTimerProviderTest COMMAND dasall_posix_timer_provider_unit_test)
set_tests_properties(PosixTimerProviderTest PROPERTIES LABELS "unit")

add_executable(dasall_blocking_queue_provider_unit_test
  BlockingQueueProviderTest.cpp
)

target_link_libraries(dasall_blocking_queue_provider_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_blocking_queue_provider_unit_test)

add_test(NAME BlockingQueueProviderTest COMMAND dasall_blocking_queue_provider_unit_test)
set_tests_properties(BlockingQueueProviderTest PROPERTIES LABELS "unit")

add_executable(dasall_linux_file_system_provider_unit_test
  LinuxFileSystemProviderTest.cpp
)

target_link_libraries(dasall_linux_file_system_provider_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_linux_file_system_provider_unit_test)

add_test(NAME LinuxFileSystemProviderTest COMMAND dasall_linux_file_system_provider_unit_test)
set_tests_properties(LinuxFileSystemProviderTest PROPERTIES LABELS "unit")

add_executable(dasall_linux_network_provider_unit_test
  LinuxNetworkProviderTest.cpp
)

target_link_libraries(dasall_linux_network_provider_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_linux_network_provider_unit_test)

add_test(NAME LinuxNetworkProviderTest COMMAND dasall_linux_network_provider_unit_test)
set_tests_properties(LinuxNetworkProviderTest PROPERTIES LABELS "unit")

add_executable(dasall_unix_ipc_provider_unit_test
  UnixIpcProviderTest.cpp
)

target_link_libraries(dasall_unix_ipc_provider_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_unix_ipc_provider_unit_test)

add_test(NAME UnixIpcProviderTest COMMAND dasall_unix_ipc_provider_unit_test)
set_tests_properties(UnixIpcProviderTest PROPERTIES LABELS "unit")

add_executable(dasall_unix_ipc_provider_peer_identity_unit_test
  UnixIpcProviderPeerIdentityTest.cpp
)

target_link_libraries(dasall_unix_ipc_provider_peer_identity_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_unix_ipc_provider_peer_identity_unit_test)

add_test(NAME UnixIpcProviderPeerIdentityTest COMMAND dasall_unix_ipc_provider_peer_identity_unit_test)
set_tests_properties(UnixIpcProviderPeerIdentityTest PROPERTIES LABELS "unit")

add_executable(dasall_unix_ipc_provider_loopback_unit_test
  UnixIpcProviderLoopbackTest.cpp
)

target_link_libraries(dasall_unix_ipc_provider_loopback_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_unix_ipc_provider_loopback_unit_test)

add_test(NAME UnixIpcProviderLoopbackTest COMMAND dasall_unix_ipc_provider_loopback_unit_test)
set_tests_properties(UnixIpcProviderLoopbackTest PROPERTIES LABELS "unit")

add_executable(dasall_hal_availability_bridge_unit_test
  HalAvailabilityBridgeTest.cpp
)

target_link_libraries(dasall_hal_availability_bridge_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_hal_availability_bridge_unit_test)

add_test(NAME HalAvailabilityBridgeTest COMMAND dasall_hal_availability_bridge_unit_test)
set_tests_properties(HalAvailabilityBridgeTest PROPERTIES LABELS "unit")

add_executable(dasall_posix_dynamic_library_loader_unit_test
  PosixDynamicLibraryLoaderTest.cpp
)

target_link_libraries(dasall_posix_dynamic_library_loader_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

target_link_options(dasall_posix_dynamic_library_loader_unit_test
  PRIVATE
    -Wl,--export-dynamic
)

dasall_apply_common_options(dasall_posix_dynamic_library_loader_unit_test)

add_test(NAME PosixDynamicLibraryLoaderTest COMMAND dasall_posix_dynamic_library_loader_unit_test)
set_tests_properties(PosixDynamicLibraryLoaderTest PROPERTIES LABELS "unit")
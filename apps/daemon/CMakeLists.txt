add_executable(dasall-daemon
  src/main.cpp
  src/DaemonBootstrap.cpp
  src/DaemonConfig.h
  src/DaemonEntryConfigLoader.cpp
  src/DaemonEntryConfigLoader.h
  src/DaemonConfigReloader.cpp
  src/DaemonConfigReloader.h
  src/DaemonConfigValidator.cpp
  src/DaemonConfigValidator.h
  src/DaemonSocketPolicy.cpp
  src/DaemonSocketPolicy.h
  src/DaemonLifecycleController.cpp
  src/DaemonLifecycleController.h
  src/DaemonListenerHost.cpp
  src/DaemonListenerHost.h
  src/DaemonSignalHandler.cpp
  src/DaemonSignalHandler.h
  src/DaemonSupervisorAdapter.cpp
  src/DaemonSupervisorAdapter.h
)

target_compile_features(dasall-daemon PRIVATE cxx_std_20)

target_link_libraries(dasall-daemon
  PRIVATE
    dasall_access
    dasall_apps_runtime_support
    dasall_runtime
    dasall_contracts
    dasall_infra
    dasall_profiles
    dasall_platform
)

target_include_directories(dasall-daemon
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/infra/src
)

set_target_properties(dasall-daemon PROPERTIES
  OUTPUT_NAME dasall-daemon
)

install(TARGETS dasall-daemon
  RUNTIME DESTINATION ${DASALL_INSTALL_SBINDIR}
)

dasall_apply_common_options(dasall-daemon)

add_executable(dasall_memory_maintenance_proof_tool
  src/MemoryMaintenanceProofMain.cpp
  src/MemoryMaintenanceProofRunner.cpp
  src/DaemonEntryConfigLoader.cpp
)

target_compile_features(dasall_memory_maintenance_proof_tool PRIVATE cxx_std_20)

target_link_libraries(dasall_memory_maintenance_proof_tool
  PRIVATE
    dasall_memory
    dasall_profiles
    dasall_infra
)

target_include_directories(dasall_memory_maintenance_proof_tool
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/infra/src
)

set_target_properties(dasall_memory_maintenance_proof_tool PROPERTIES
  OUTPUT_NAME dasall-memory-maintenance-proof
)

install(TARGETS dasall_memory_maintenance_proof_tool
  RUNTIME DESTINATION lib/dasall
)

dasall_apply_common_options(dasall_memory_maintenance_proof_tool)

add_executable(dasall_memory_installed_proof_tool
  src/MemoryInstalledProofMain.cpp
  src/MemoryInstalledProofRunner.cpp
  src/DaemonEntryConfigLoader.cpp
)

target_compile_features(dasall_memory_installed_proof_tool PRIVATE cxx_std_20)

target_link_libraries(dasall_memory_installed_proof_tool
  PRIVATE
    dasall_memory
    dasall_profiles
    dasall_infra
)

target_include_directories(dasall_memory_installed_proof_tool
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/infra/src
)

set_target_properties(dasall_memory_installed_proof_tool PROPERTIES
  OUTPUT_NAME dasall-memory-installed-proof
)

install(TARGETS dasall_memory_installed_proof_tool
  RUNTIME DESTINATION lib/dasall
)

dasall_apply_common_options(dasall_memory_installed_proof_tool)

add_executable(dasall_tools_installed_proof_tool
  src/ToolsInstalledProofMain.cpp
  src/ToolsInstalledProofRunner.cpp
  src/DaemonEntryConfigLoader.cpp
)

target_compile_features(dasall_tools_installed_proof_tool PRIVATE cxx_std_20)

target_link_libraries(dasall_tools_installed_proof_tool
  PRIVATE
    dasall_apps_runtime_support
    dasall_runtime
    dasall_tools
    dasall_services
    dasall_profiles
    dasall_infra
    dasall_contracts
)

target_include_directories(dasall_tools_installed_proof_tool
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/infra/src
)

set_target_properties(dasall_tools_installed_proof_tool PROPERTIES
  OUTPUT_NAME dasall-tools-installed-proof
)

install(TARGETS dasall_tools_installed_proof_tool
  RUNTIME DESTINATION lib/dasall
)

dasall_apply_common_options(dasall_tools_installed_proof_tool)

add_executable(dasall_runtime_installed_proof_tool
  src/RuntimeInstalledProofMain.cpp
  src/RuntimeInstalledProofRunner.cpp
  src/DaemonEntryConfigLoader.cpp
)

target_compile_features(dasall_runtime_installed_proof_tool PRIVATE cxx_std_20)

target_link_libraries(dasall_runtime_installed_proof_tool
  PRIVATE
    dasall_apps_runtime_support
    dasall_cognition
    dasall_runtime
    dasall_tools
    dasall_profiles
    dasall_infra
    dasall_contracts
)

target_include_directories(dasall_runtime_installed_proof_tool
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/access/include
    ${CMAKE_SOURCE_DIR}/cognition/include
    ${CMAKE_SOURCE_DIR}/infra/src
    ${CMAKE_SOURCE_DIR}/memory/include
    ${CMAKE_SOURCE_DIR}/runtime/src
)

set_target_properties(dasall_runtime_installed_proof_tool PROPERTIES
  OUTPUT_NAME dasall-runtime-installed-proof
)

install(TARGETS dasall_runtime_installed_proof_tool
  RUNTIME DESTINATION lib/dasall
)

dasall_apply_common_options(dasall_runtime_installed_proof_tool)



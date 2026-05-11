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



add_executable(dasall_simulator 
  src/main.cpp
  src/SimulatorProtocolAdapter.cpp
)

target_compile_features(dasall_simulator PRIVATE cxx_std_20)

target_include_directories(dasall_simulator
  PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/access/src
)

target_link_libraries(dasall_simulator
  PRIVATE
    dasall_access
    dasall_runtime
    dasall_contracts
    dasall_infra
)

dasall_apply_common_options(dasall_simulator)

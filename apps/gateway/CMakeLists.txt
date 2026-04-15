add_executable(dasall_gateway src/main.cpp)

target_compile_features(dasall_gateway PRIVATE cxx_std_20)

target_link_libraries(dasall_gateway
  PRIVATE
    dasall_access
    dasall_runtime
    dasall_contracts
    dasall_infra
)

dasall_apply_common_options(dasall_gateway)

add_executable(dasall_cli src/main.cpp)

target_compile_features(dasall_cli PRIVATE cxx_std_20)

target_link_libraries(dasall_cli
  PRIVATE
    dasall_access
    dasall_runtime
    dasall_contracts
    dasall_infra
)

dasall_apply_common_options(dasall_cli)

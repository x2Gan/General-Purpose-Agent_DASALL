add_library(dasall_platform STATIC)

target_sources(dasall_platform
  PRIVATE
    src/linux/CapabilityRegistry.cpp
    src/linux/LinuxPlatformFactory.cpp
    src/linux/PosixThreadProvider.cpp
    src/placeholder.cpp
)

target_compile_features(dasall_platform PUBLIC cxx_std_20)

target_include_directories(dasall_platform
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dasall_platform
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_platform)

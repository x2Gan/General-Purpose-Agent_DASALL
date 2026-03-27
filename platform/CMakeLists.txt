add_library(dasall_platform STATIC)

target_sources(dasall_platform
  PRIVATE
    src/arm/hal/HalStub.cpp
    src/linux/BlockingQueueProvider.cpp
    src/linux/CapabilityRegistry.cpp
    src/linux/HalAvailabilityBridge.cpp
    src/linux/LinuxFileSystemProvider.cpp
    src/linux/LinuxNetworkProvider.cpp
    src/linux/LinuxPlatformFactory.cpp
    src/linux/PosixThreadProvider.cpp
    src/linux/PosixTimerProvider.cpp
    src/linux/UnixIpcProvider.cpp
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

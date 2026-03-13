if(TARGET dasall_build_options)
  return()
endif()

add_library(dasall_build_options INTERFACE)

target_compile_features(dasall_build_options INTERFACE cxx_std_20)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i[3-6]86)$")
  target_compile_definitions(dasall_build_options INTERFACE DASALL_ARCH_X86=1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|armv7|armv8|arm)$")
  target_compile_definitions(dasall_build_options INTERFACE DASALL_ARCH_ARM=1)
else()
  target_compile_definitions(dasall_build_options INTERFACE DASALL_ARCH_GENERIC=1)
endif()

if(UNIX AND NOT APPLE)
  target_compile_definitions(dasall_build_options INTERFACE DASALL_PLATFORM_LINUX=1)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(dasall_build_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wformat=2
    -Werror=return-type
    -fPIC
    -ffunction-sections
    -fdata-sections
    $<$<CONFIG:Debug>:-O0;-g3>
    $<$<CONFIG:RelWithDebInfo>:-O2;-g>
    $<$<CONFIG:Release>:-O2;-DNDEBUG>
  )

  target_link_options(dasall_build_options INTERFACE
    -Wl,--gc-sections
  )
elseif(MSVC)
  target_compile_options(dasall_build_options INTERFACE /W4)
endif()

function(dasall_apply_common_options target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "Target not found: ${target_name}")
  endif()

  target_link_libraries(${target_name} PUBLIC dasall_build_options)
endfunction()

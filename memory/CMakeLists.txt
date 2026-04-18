set(DASALL_MEMORY_PUBLIC_INCLUDE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(DASALL_MEMORY_PUBLIC_INCLUDE_SUBDIRECTORIES
  config
  context
  error
  vector
  working
  writeback
)

set(DASALL_MEMORY_SKELETON_SOURCES
  src/MemoryBuildSkeleton.cpp
  src/MemoryManager.cpp
  src/MemoryManagerFactory.cpp
  src/working/WorkingMemoryBoard.cpp
  src/vector/UnavailableVectorMemoryIndexAdapter.cpp
)

if(NOT IS_DIRECTORY ${DASALL_MEMORY_PUBLIC_INCLUDE_DIR})
  message(FATAL_ERROR "dasall_memory requires a public include root at ${DASALL_MEMORY_PUBLIC_INCLUDE_DIR}")
endif()

foreach(subdirectory IN LISTS DASALL_MEMORY_PUBLIC_INCLUDE_SUBDIRECTORIES)
  if(NOT IS_DIRECTORY ${DASALL_MEMORY_PUBLIC_INCLUDE_DIR}/${subdirectory})
    message(FATAL_ERROR "dasall_memory requires public include subdirectory ${DASALL_MEMORY_PUBLIC_INCLUDE_DIR}/${subdirectory}")
  endif()
endforeach()

foreach(source_file IN LISTS DASALL_MEMORY_SKELETON_SOURCES)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${source_file})
    message(FATAL_ERROR "dasall_memory skeleton source is missing: ${source_file}")
  endif()
endforeach()

add_library(dasall_memory STATIC)

target_sources(dasall_memory
  PRIVATE
    ${DASALL_MEMORY_SKELETON_SOURCES}
)

target_compile_features(dasall_memory PUBLIC cxx_std_20)

target_include_directories(dasall_memory
  PUBLIC
    $<BUILD_INTERFACE:${DASALL_MEMORY_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dasall_memory
  PUBLIC
    dasall_contracts
)

dasall_apply_common_options(dasall_memory)

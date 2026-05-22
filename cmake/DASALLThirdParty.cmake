include(FetchContent)

set(DASALL_THIRD_PARTY_ROOT "${CMAKE_SOURCE_DIR}/third_party")
set(DASALL_THIRD_PARTY_CACHE_DIR "${DASALL_THIRD_PARTY_ROOT}/.cache" CACHE PATH "Local cache for third-party sources")

option(DASALL_USE_SUBMODULES "Enable submodule-based third-party resolution" ON)
option(DASALL_USE_LOCAL_CACHE "Enable local cache third-party resolution" ON)
option(DASALL_ALLOW_FETCHCONTENT "Enable FetchContent fallback for third-party dependencies" ON)
option(DASALL_ENABLE_TUI_FTXUI "Resolve FTXUI for apps/tui when the TUI target is enabled" OFF)

# Off by default to keep configure reproducible in offline environments.
option(DASALL_BOOTSTRAP_THIRD_PARTY "Try to resolve third-party dependencies during configure" OFF)

set(DASALL_FTXUI_GIT_REPOSITORY "https://github.com/ArthurSonzogni/FTXUI.git" CACHE STRING "FTXUI upstream repository")
set(DASALL_FTXUI_GIT_TAG "5cfed50702f52d51c1b189b5f97f8beaf5eaa2a6" CACHE STRING "Pinned FTXUI commit (v6.1.9)")

file(MAKE_DIRECTORY "${DASALL_THIRD_PARTY_CACHE_DIR}")

function(dasall_resolve_dependency)
  set(options)
  set(oneValueArgs NAME SUBMODULE_DIR CACHE_DIR FETCH_GIT_REPOSITORY FETCH_GIT_TAG SOURCE_SUBDIR)
  set(multiValueArgs)
  cmake_parse_arguments(DASALL_DEP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT DASALL_DEP_NAME)
    message(FATAL_ERROR "dasall_resolve_dependency requires NAME")
  endif()

  if(NOT DASALL_DEP_SUBMODULE_DIR)
    set(DASALL_DEP_SUBMODULE_DIR "${DASALL_THIRD_PARTY_ROOT}/${DASALL_DEP_NAME}")
  endif()

  if(NOT DASALL_DEP_CACHE_DIR)
    set(DASALL_DEP_CACHE_DIR "${DASALL_THIRD_PARTY_CACHE_DIR}/${DASALL_DEP_NAME}")
  endif()

  if(NOT DASALL_DEP_SOURCE_SUBDIR)
    set(DASALL_DEP_SOURCE_SUBDIR "")
  endif()

  set(_resolved FALSE)

  if(DASALL_USE_SUBMODULES)
    if(EXISTS "${DASALL_DEP_SUBMODULE_DIR}/CMakeLists.txt")
      add_subdirectory(
        "${DASALL_DEP_SUBMODULE_DIR}"
        "${CMAKE_BINARY_DIR}/_deps/submodule_${DASALL_DEP_NAME}"
        EXCLUDE_FROM_ALL
      )
      set(_resolved TRUE)
      set(_source "submodule")
    endif()
  endif()

  if(NOT _resolved AND DASALL_USE_LOCAL_CACHE)
    if(EXISTS "${DASALL_DEP_CACHE_DIR}/CMakeLists.txt")
      add_subdirectory(
        "${DASALL_DEP_CACHE_DIR}"
        "${CMAKE_BINARY_DIR}/_deps/cache_${DASALL_DEP_NAME}"
        EXCLUDE_FROM_ALL
      )
      set(_resolved TRUE)
      set(_source "local-cache")
    endif()
  endif()

  if(NOT _resolved AND DASALL_ALLOW_FETCHCONTENT)
    if(DASALL_DEP_FETCH_GIT_REPOSITORY AND DASALL_DEP_FETCH_GIT_TAG)
      set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

      FetchContent_Declare(${DASALL_DEP_NAME}
        GIT_REPOSITORY ${DASALL_DEP_FETCH_GIT_REPOSITORY}
        GIT_TAG ${DASALL_DEP_FETCH_GIT_TAG}
        SOURCE_SUBDIR ${DASALL_DEP_SOURCE_SUBDIR}
      )
      FetchContent_MakeAvailable(${DASALL_DEP_NAME})
      set(_resolved TRUE)
      set(_source "fetchcontent")
    endif()
  endif()

  if(_resolved)
    message(STATUS "Dependency ${DASALL_DEP_NAME} resolved from ${_source}")
    set(${DASALL_DEP_NAME}_SOURCE ${_source} PARENT_SCOPE)
    return()
  endif()

  message(WARNING
    "Dependency ${DASALL_DEP_NAME} not resolved. Expected priority: submodule > local cache > FetchContent. "
    "Check third_party, third_party/.cache, or fetch settings."
  )
endfunction()

# cpp-httplib: header-only HTTP library used by dasall_gateway (ACC-TODO-026)
dasall_resolve_dependency(
  NAME cpp-httplib
  FETCH_GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  FETCH_GIT_TAG v0.15.3
)

# FTXUI: terminal UI framework reserved for apps/tui only (TUI-TODO-005).
if(DASALL_ENABLE_TUI_FTXUI)
  dasall_resolve_dependency(
    NAME ftxui
    FETCH_GIT_REPOSITORY ${DASALL_FTXUI_GIT_REPOSITORY}
    FETCH_GIT_TAG ${DASALL_FTXUI_GIT_TAG}
  )
endif()


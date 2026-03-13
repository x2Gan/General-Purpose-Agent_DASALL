# DASALL Third-Party Dependency Strategy

Dependency resolution priority:

1. Submodule: third_party/<name>
2. Local cache: third_party/.cache/<name>
3. FetchContent fallback

Notes:

- Local cache is intentionally outside the build directory, so normal build cleanup does not remove it.
- Configure options:
  - DASALL_USE_SUBMODULES=ON/OFF
  - DASALL_USE_LOCAL_CACHE=ON/OFF
  - DASALL_ALLOW_FETCHCONTENT=ON/OFF
  - DASALL_BOOTSTRAP_THIRD_PARTY=ON/OFF

Implementation entry:

- cmake/DASALLThirdParty.cmake

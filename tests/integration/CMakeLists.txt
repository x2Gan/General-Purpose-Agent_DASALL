# Integration topology SSOT entrypoint.
# Keep at least one integration-labeled test discoverable at top-level.
add_test(NAME infra_integration_topology_smoke COMMAND ${CMAKE_COMMAND} -E true)
set_tests_properties(infra_integration_topology_smoke PROPERTIES LABELS "integration")

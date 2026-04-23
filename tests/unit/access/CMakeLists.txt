set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_access_unit_test target_name test_name source_file)
  add_executable(${target_name}
    ${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_access
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "unit;access")

  list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
    ${target_name}
  )
endmacro()

dasall_add_access_unit_test(
  dasall_access_interface_surface_unit_test
  AccessInterfaceSurfaceTest
  AccessInterfaceSurfaceTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_error_code_unit_test
  AccessErrorCodeTest
  AccessErrorCodeTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_error_mapping_unit_test
  AccessErrorMappingTest
  AccessErrorMappingTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_gateway_lifecycle_unit_test
  AccessGatewayLifecycleTest
  AccessGatewayLifecycleTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_supporting_types_unit_test
  AccessSupportingTypesTest
  AccessSupportingTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_publish_envelope_unit_test
  PublishEnvelopeTypesTest
  PublishEnvelopeTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_async_task_receipt_unit_test
  AsyncTaskReceiptTypesTest
  AsyncTaskReceiptTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_admission_controller_surface_unit_test
  AdmissionControllerSurfaceTest
  AdmissionControllerSurfaceTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_runtime_bridge_surface_unit_test
  RuntimeBridgeSurfaceTest
  RuntimeBridgeSurfaceTest.cpp
)

if(NOT DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "access unit registration requires at least one test target")
endif()

set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  ${DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)

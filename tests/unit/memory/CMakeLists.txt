add_executable(dasall_memory_interface_compile_unit_test
	MemoryInterfaceCompileTest.cpp
)

target_link_libraries(dasall_memory_interface_compile_unit_test
	PRIVATE
		dasall_memory
		dasall_test_support
)

target_compile_definitions(dasall_memory_interface_compile_unit_test
	PRIVATE
		DASALL_MEMORY_PUBLIC_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/memory/include"
		DASALL_MEMORY_SOURCE_DIR="${CMAKE_SOURCE_DIR}/memory/src"
)

dasall_apply_common_options(dasall_memory_interface_compile_unit_test)

add_test(NAME MemoryInterfaceCompileTest COMMAND dasall_memory_interface_compile_unit_test)
set_tests_properties(MemoryInterfaceCompileTest PROPERTIES LABELS "unit;memory")

set(DASALL_MEMORY_UNIT_TEST_EXECUTABLE_TARGETS
	dasall_memory_interface_compile_unit_test
	PARENT_SCOPE
)

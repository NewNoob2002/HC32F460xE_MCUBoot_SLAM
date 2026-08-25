add_executable(memory_map_tests memory_map_tests.c)
target_include_directories(memory_map_tests PRIVATE "${CMAKE_BINARY_DIR}/generated/Config")
add_test(NAME memory_map COMMAND memory_map_tests)

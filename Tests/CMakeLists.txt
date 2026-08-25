add_executable(memory_map_tests memory_map_tests.c)
target_include_directories(memory_map_tests PRIVATE "${CMAKE_BINARY_DIR}/generated/Config")
add_test(NAME memory_map COMMAND memory_map_tests)

add_executable(flash_map_tests
    flash_map_tests.c
    ${PROJECT_SOURCE_DIR}/components/mcuboot_port/Src/flash_map_backend.c
)
target_include_directories(flash_map_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/components/mcuboot_port/Inc"
    "${PROJECT_SOURCE_DIR}/Platform/HC32F460/Inc"
    "${CMAKE_BINARY_DIR}/generated/Config"
)
add_test(NAME flash_map COMMAND flash_map_tests)

add_executable(boot_handover_tests
    boot_handover_tests.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/boot_handover.c
)
target_include_directories(boot_handover_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/Platform/HC32F460/Inc"
)
target_compile_definitions(boot_handover_tests PRIVATE BOOT_HOST_TEST)
add_test(NAME boot_handover COMMAND boot_handover_tests)

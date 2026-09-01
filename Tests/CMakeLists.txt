add_executable(memory_map_tests memory_map_tests.c)
target_include_directories(memory_map_tests PRIVATE "${CMAKE_BINARY_DIR}/generated/Config")
add_test(NAME memory_map COMMAND memory_map_tests)

add_executable(flash_map_tests
    flash_map_tests.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Ports/mcuboot/Src/flash_map_backend.c
)
target_include_directories(flash_map_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/Platform/HC32F460/Ports/mcuboot/Inc"
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
    "${PROJECT_SOURCE_DIR}/Platform/HC32F460/Ports/mcuboot/Inc"
    "${CMAKE_BINARY_DIR}/generated/Config"
)
target_compile_definitions(boot_handover_tests PRIVATE BOOT_HOST_TEST)
add_test(NAME boot_handover COMMAND boot_handover_tests)

add_executable(status_led_tests
    status_led_tests.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/bsp_status_led.c
)
target_include_directories(status_led_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/Platform/HC32F460/Inc"
)
target_compile_definitions(status_led_tests PRIVATE BOOT_HOST_TEST)
add_test(NAME status_led COMMAND status_led_tests)

add_executable(app_confirm_tests
    app_confirm_tests.c
    ${PROJECT_SOURCE_DIR}/App/Core/Src/app_confirm.c
)
target_include_directories(app_confirm_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/App/Core/Inc"
    "${PROJECT_SOURCE_DIR}/components/mcuboot-2.4.0/boot/bootutil/include"
    "${PROJECT_SOURCE_DIR}/Platform/HC32F460/Ports/mcuboot/Inc"
    "${CMAKE_BINARY_DIR}/generated/Config"
)
add_test(NAME app_confirm COMMAND app_confirm_tests)

add_executable(fw_update_contract_tests
    fw_update_contract_tests.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/storage.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/boot_control.c
)
target_include_directories(fw_update_contract_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/components/fw_update/include
)
add_test(NAME fw_update_contract COMMAND fw_update_contract_tests)

set(FLASHDB_ROOT ${PROJECT_SOURCE_DIR}/components/FlashDB-2.2.0)
set(FLASHDB_TEST_SOURCES
    ${FLASHDB_ROOT}/src/fdb.c
    ${FLASHDB_ROOT}/src/fdb_kvdb.c
    ${FLASHDB_ROOT}/src/fdb_utils.c
    ${FLASHDB_ROOT}/port/fal/src/fal.c
    ${FLASHDB_ROOT}/port/fal/src/fal_flash.c
    ${FLASHDB_ROOT}/port/fal/src/fal_partition.c
)
add_executable(product_config_flashdb_tests
    product_config_flashdb_tests.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/product_config.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/backends/flashdb/src/product_config_flashdb.c
    ${FLASHDB_TEST_SOURCES}
)
set_source_files_properties(${FLASHDB_TEST_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(product_config_flashdb_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/components/fw_update/include
    ${PROJECT_SOURCE_DIR}/components/fw_update/backends/flashdb/include
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Ports/flashdb/Inc
    ${FLASHDB_ROOT}/inc
    ${FLASHDB_ROOT}/port/fal/inc
    ${CMAKE_BINARY_DIR}/generated/Config
)
add_test(NAME product_config_flashdb COMMAND product_config_flashdb_tests)

add_executable(fw_update_mcuboot_backend_tests
    fw_update_mcuboot_backend_tests.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/storage.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/boot_control.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/product_config.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/backends/mcuboot/src/storage_mcuboot.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/backends/mcuboot/src/boot_control_mcuboot.c
)
target_include_directories(fw_update_mcuboot_backend_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/components/fw_update/include
    ${PROJECT_SOURCE_DIR}/components/fw_update/backends/mcuboot/include
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Ports/mcuboot/Inc
    ${CMAKE_BINARY_DIR}/generated/Config
)
target_include_directories(fw_update_mcuboot_backend_tests SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/components/mcuboot-2.4.0/boot/bootutil/include
    ${PROJECT_SOURCE_DIR}/components/mcuboot-2.4.0/boot/bootutil/src
)
add_test(NAME fw_update_mcuboot_backend COMMAND fw_update_mcuboot_backend_tests)

add_test(
    NAME product_identity_production_gate
    COMMAND ${CMAKE_COMMAND}
        -DHC32_PRODUCT_IDENTITY_FILE=${PROJECT_SOURCE_DIR}/Config/Product/ProductIdentity.env
        -DHC32_PRODUCT_RELEASE=ON
        -P ${PROJECT_SOURCE_DIR}/cmake/ProductIdentity.cmake
)
add_test(
    NAME product_identity_rejects_nonproduction
    COMMAND ${CMAKE_COMMAND}
        -DHC32_PRODUCT_IDENTITY_FILE=${PROJECT_SOURCE_DIR}/Tests/ProductIdentityNonProduction.env
        -DHC32_PRODUCT_RELEASE=ON
        -P ${PROJECT_SOURCE_DIR}/cmake/ProductIdentity.cmake
)
set_tests_properties(product_identity_rejects_nonproduction PROPERTIES WILL_FAIL TRUE)

add_executable(fw_protocol_tests
    fw_protocol_tests.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/protocol.c
)
target_include_directories(fw_protocol_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/components/fw_update/include
)
add_test(NAME fw_protocol COMMAND fw_protocol_tests)

add_executable(fw_update_manager_tests
    fw_update_manager_tests.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/manager.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/protocol.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/storage.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/boot_control.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/product_config.c
)
target_include_directories(fw_update_manager_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/components/fw_update/include
)
add_test(NAME fw_update_manager COMMAND fw_update_manager_tests)

add_executable(fw_update_fake_device
    fw_update_fake_device.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/manager.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/protocol.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/storage.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/boot_control.c
    ${PROJECT_SOURCE_DIR}/components/fw_update/src/product_config.c
)
target_include_directories(fw_update_fake_device PRIVATE
    ${PROJECT_SOURCE_DIR}/components/fw_update/include
)

find_program(CARGO_EXECUTABLE cargo REQUIRED)
add_test(
    NAME rust_updater
    COMMAND ${CMAKE_COMMAND} -E env
        "HC32_FAKE_DEVICE=$<TARGET_FILE:fw_update_fake_device>"
        ${CARGO_EXECUTABLE} test
        --manifest-path ${PROJECT_SOURCE_DIR}/Tools/updater/Cargo.toml
        --locked
        --features fake-e2e
)
set_tests_properties(rust_updater PROPERTIES TIMEOUT 300)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME fw_update_architecture
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Architecture/check_portable_dependencies.py
)
add_test(
    NAME application_platform_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Architecture/check_application_platform_boundary.py
)
add_test(
    NAME usb_evaluation_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Architecture/check_usb_boundary.py
)
add_test(
    NAME usb_loopback_host_self_test
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tools/host/usb_loopback.py self-test
)

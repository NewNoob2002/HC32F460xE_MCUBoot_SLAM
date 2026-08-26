option(APP_AUTO_CONFIRM "Confirm the running MCUboot image at startup" ON)
set(APP_VERSION "1.0.0" CACHE STRING "MCUboot application image version")
set(APP_PHASE2_HIL_MODE "0" CACHE STRING "Phase 2 HIL mode: 0=off, 1=storage, 2=pending")
if(NOT APP_PHASE2_HIL_MODE MATCHES "^[012]$")
    message(FATAL_ERROR "APP_PHASE2_HIL_MODE must be 0, 1 or 2")
endif()

add_executable(app_firmware
    Core/Src/app_confirm.c
    Core/Src/main.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/syscalls.c
    $<TARGET_OBJECTS:hc32_startup_app>
)
set_target_properties(app_firmware PROPERTIES SUFFIX .elf)
target_include_directories(app_firmware PRIVATE Core/Inc)
target_compile_definitions(app_firmware PRIVATE
    APP_AUTO_CONFIRM=$<BOOL:${APP_AUTO_CONFIRM}>
    APP_PHASE2_HIL_MODE=${APP_PHASE2_HIL_MODE}
)
target_link_libraries(app_firmware PRIVATE hc32_platform hc32_device mcuboot_bootutil)
if(NOT APP_PHASE2_HIL_MODE STREQUAL "0")
    target_sources(app_firmware PRIVATE ${PROJECT_SOURCE_DIR}/Tests/HIL/phase2_hil.c)
    target_include_directories(app_firmware PRIVATE ${PROJECT_SOURCE_DIR}/Tests/HIL)
    target_link_libraries(app_firmware PRIVATE fw_update_mcuboot)
endif()
target_link_options(app_firmware PRIVATE
    -T${PROJECT_SOURCE_DIR}/Linker/app.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/app_firmware.map
)
add_firmware_artifacts(app_firmware)
add_mcuboot_signed_artifacts(app_firmware)

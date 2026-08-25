add_executable(app_firmware
    Core/Src/main.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/syscalls.c
    $<TARGET_OBJECTS:hc32_startup_app>
)
set_target_properties(app_firmware PROPERTIES SUFFIX .elf)
target_include_directories(app_firmware PRIVATE Core/Inc)
target_link_libraries(app_firmware PRIVATE hc32_platform hc32_device)
target_link_options(app_firmware PRIVATE
    -T${PROJECT_SOURCE_DIR}/Linker/app.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/app_firmware.map
)
add_firmware_artifacts(app_firmware)

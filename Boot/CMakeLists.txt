add_executable(boot_firmware
    Core/Src/main.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/syscalls.c
    $<TARGET_OBJECTS:hc32_startup_boot>
)
set_target_properties(boot_firmware PROPERTIES SUFFIX .elf)
target_include_directories(boot_firmware PRIVATE Core/Inc)
target_link_libraries(boot_firmware PRIVATE hc32_platform hc32_device mcuboot_bootutil)
target_link_options(boot_firmware PRIVATE
    -T${PROJECT_SOURCE_DIR}/Linker/boot.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/boot_firmware.map
)
add_firmware_artifacts(boot_firmware)

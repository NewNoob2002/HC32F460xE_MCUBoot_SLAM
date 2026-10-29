set(BOOT_VERSION "1.1.0" CACHE STRING "Bootloader version reported over Protocol V1")
hc32_parse_version(BOOT_VERSION "${BOOT_VERSION}")
add_executable(boot_firmware
    Core/Src/boot_info.c
    Core/Src/main.c
    ${PROJECT_SOURCE_DIR}/App/Core/Src/usb_fw_update.c
    $<TARGET_OBJECTS:hc32_boot_fault>
    $<TARGET_OBJECTS:hc32_newlib>
    $<TARGET_OBJECTS:hc32_startup_boot>
    ${HC32_ADMIN_PUBLIC_KEY_SOURCE}
)
set_target_properties(boot_firmware PROPERTIES SUFFIX .elf)
target_include_directories(boot_firmware PRIVATE
    Core/Inc
    ${PROJECT_SOURCE_DIR}/App/Core/Inc
)
target_include_directories(boot_firmware SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/components/cherryusb/common
    ${PROJECT_SOURCE_DIR}/components/cherryusb/core
)
target_compile_definitions(boot_firmware PRIVATE
    USB_FW_UPDATE_BOOT_RECOVERY=1
    BOOT_VERSION_MAJOR=${BOOT_VERSION_MAJOR_VALUE}
    BOOT_VERSION_MINOR=${BOOT_VERSION_MINOR_VALUE}
    BOOT_VERSION_REVISION=${BOOT_VERSION_REVISION_VALUE}
    BOOT_VERSION_BUILD=${BOOT_VERSION_BUILD_VALUE}
    $<$<BOOL:${HC32_ADMIN_PUBLIC_KEY_SOURCE}>:HC32_ADMIN_AUTH_ENABLED=1>
)
set_source_files_properties(
    Core/Src/boot_info.c
    Core/Src/main.c
    ${PROJECT_SOURCE_DIR}/App/Core/Src/usb_fw_update.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wconversion;-Wshadow;-Werror"
)
target_link_libraries(boot_firmware PRIVATE
    cherryusb_device
    fw_update_mcuboot
    hc32_usb_device_port
    hc32_device
    $<$<CONFIG:Debug>:easylogger_port_hc32>
    mcuboot_bootutil
)
target_link_options(boot_firmware PRIVATE
    -T${PROJECT_SOURCE_DIR}/Linker/boot.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/boot_firmware.map
)
add_firmware_artifacts(boot_firmware)

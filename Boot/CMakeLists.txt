add_executable(boot_firmware
    Core/Src/hc32f460_it.c
    Core/Src/main.c
    ${PROJECT_SOURCE_DIR}/App/Core/Src/usb_fw_update.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/usb_dc_hc32f460.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/syscalls.c
    $<TARGET_OBJECTS:hc32_startup_boot>
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
    APP_VERSION_MAJOR=0
    APP_VERSION_MINOR=0
    APP_VERSION_REVISION=0
    APP_VERSION_BUILD=0
)
set_source_files_properties(
    Core/Src/hc32f460_it.c
    Core/Src/main.c
    ${PROJECT_SOURCE_DIR}/App/Core/Src/usb_fw_update.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wconversion;-Wshadow;-Werror"
)
target_link_libraries(boot_firmware PRIVATE
    cherryusb_device
    fw_update_mcuboot
    hc32_platform
    hc32_device
    $<$<CONFIG:Debug>:hc32_easylogger>
    mcuboot_bootutil
)
target_link_options(boot_firmware PRIVATE
    -T${PROJECT_SOURCE_DIR}/Linker/boot.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/boot_firmware.map
)
add_firmware_artifacts(boot_firmware)

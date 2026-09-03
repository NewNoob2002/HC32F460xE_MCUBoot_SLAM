option(APP_AUTO_CONFIRM "Confirm the running MCUboot image at startup" ON)
set(APP_VERSION "1.0.0" CACHE STRING "MCUboot application image version")
if(NOT APP_VERSION MATCHES "^([0-9]+)[.]([0-9]+)[.]([0-9]+)([+]([0-9]+))?$")
    message(FATAL_ERROR "APP_VERSION must be MAJOR.MINOR.REVISION[+BUILD]")
endif()
set(APP_VERSION_MAJOR_VALUE ${CMAKE_MATCH_1})
set(APP_VERSION_MINOR_VALUE ${CMAKE_MATCH_2})
set(APP_VERSION_REVISION_VALUE ${CMAKE_MATCH_3})
if("${CMAKE_MATCH_5}" STREQUAL "")
    set(APP_VERSION_BUILD_VALUE 0)
else()
    set(APP_VERSION_BUILD_VALUE ${CMAKE_MATCH_5})
endif()
if(APP_VERSION_MAJOR_VALUE GREATER 255 OR APP_VERSION_MINOR_VALUE GREATER 255
    OR APP_VERSION_REVISION_VALUE GREATER 65535 OR APP_VERSION_BUILD_VALUE GREATER 4294967295)
    message(FATAL_ERROR "APP_VERSION fields exceed Protocol V1 widths")
endif()
set(APP_PHASE2_HIL_MODE "0" CACHE STRING "Phase 2 HIL mode: 0=off, 1=storage, 2=pending")
if(NOT APP_PHASE2_HIL_MODE MATCHES "^[012]$")
    message(FATAL_ERROR "APP_PHASE2_HIL_MODE must be 0, 1 or 2")
endif()

add_executable(app_firmware
    Core/Src/app_confirm.c
    Core/Src/main.c
    Core/Src/usb_fw_update.c
    Devices/Src/bq40z50.c
    Diagnostics/Src/app_diagnostics.c
    Services/Src/power_devices.c
    $<TARGET_OBJECTS:hc32_newlib>
    $<TARGET_OBJECTS:hc32_startup_app>
)
set_target_properties(app_firmware PROPERTIES
    OUTPUT_NAME "app-${APP_VERSION}"
    SUFFIX .elf
)
target_include_directories(app_firmware PRIVATE
    Core/Inc
    Devices/Inc
    Diagnostics/Inc
    Services/Inc
)
target_compile_definitions(app_firmware PRIVATE
    APP_AUTO_CONFIRM=$<BOOL:${APP_AUTO_CONFIRM}>
    APP_PHASE2_HIL_MODE=${APP_PHASE2_HIL_MODE}
    APP_VERSION_MAJOR=${APP_VERSION_MAJOR_VALUE}
    APP_VERSION_MINOR=${APP_VERSION_MINOR_VALUE}
    APP_VERSION_REVISION=${APP_VERSION_REVISION_VALUE}
    APP_VERSION_BUILD=${APP_VERSION_BUILD_VALUE}
)
set_source_files_properties(
    Core/Src/main.c
    Core/Src/usb_fw_update.c
    Devices/Src/bq40z50.c
    Diagnostics/Src/app_diagnostics.c
    Services/Src/power_devices.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wconversion;-Wshadow;-Werror"
)
target_include_directories(app_firmware SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/components/cherryusb/common
    ${PROJECT_SOURCE_DIR}/components/cherryusb/core
)
target_link_libraries(app_firmware PRIVATE
    cherryusb_device
    fw_update_mcuboot
    hc32_usb_device_port
    hc32_device
    $<$<CONFIG:Debug>:easylogger_port_hc32>
    mcuboot_bootutil
)
if(NOT APP_PHASE2_HIL_MODE STREQUAL "0")
    target_sources(app_firmware PRIVATE ${PROJECT_SOURCE_DIR}/Tests/HIL/phase2_hil.c)
    target_include_directories(app_firmware PRIVATE ${PROJECT_SOURCE_DIR}/Tests/HIL)
endif()
target_link_options(app_firmware PRIVATE
    -T${PROJECT_SOURCE_DIR}/Linker/app.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/app-${APP_VERSION}.map
)
add_firmware_artifacts(app_firmware)
add_mcuboot_signed_artifacts(app_firmware)

add_custom_target(verify_newlib_syscalls
    COMMAND "${MCUBOOT_PYTHON}" "${PROJECT_SOURCE_DIR}/Tests/verify_newlib_syscalls.py"
            "${CMAKE_BINARY_DIR}/Boot/boot_firmware.map"
            "${CMAKE_CURRENT_BINARY_DIR}/app-${APP_VERSION}.map"
    DEPENDS boot_firmware app_firmware
    VERBATIM
)

add_custom_target(verify_winusb_descriptors
    COMMAND "${MCUBOOT_PYTHON}" "${PROJECT_SOURCE_DIR}/Tests/verify_winusb_descriptors.py"
            "${CMAKE_BINARY_DIR}/Boot/boot_firmware.bin"
            "${CMAKE_CURRENT_BINARY_DIR}/app-${APP_VERSION}.bin"
            "${HC32_USB_VID_VALUE}"
            "${HC32_USB_BOOT_PID_VALUE}"
            "${HC32_USB_APPLICATION_PID_VALUE}"
            "${HC32_USB_SERIAL_PREFIX}"
            "${HC32_USB_MANUFACTURER}"
            "${HC32_USB_BOOT_PRODUCT}"
            "${HC32_USB_APPLICATION_PRODUCT}"
    DEPENDS boot_firmware app_firmware
    VERBATIM
)

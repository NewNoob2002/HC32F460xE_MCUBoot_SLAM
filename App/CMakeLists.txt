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

add_executable(usb_vendor_bulk_loopback
    Core/Src/usb_vendor_bulk.c
    Core/Src/usb_vendor_bulk_main.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/usb_dc_hc32f460.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/syscalls.c
    $<TARGET_OBJECTS:hc32_startup_app>
)
set_target_properties(usb_vendor_bulk_loopback PROPERTIES SUFFIX .elf)
target_include_directories(usb_vendor_bulk_loopback PRIVATE Core/Inc)
target_link_libraries(usb_vendor_bulk_loopback PRIVATE
    cherryusb_device
    hc32_platform
    hc32_device
)
target_link_options(usb_vendor_bulk_loopback PRIVATE
    -T${PROJECT_SOURCE_DIR}/Linker/app.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/usb_vendor_bulk_loopback.map
)
add_firmware_artifacts(usb_vendor_bulk_loopback)
add_mcuboot_signed_artifacts(usb_vendor_bulk_loopback usb_loopback)

add_executable(usb_fw_updater
    Core/Src/app_confirm.c
    Core/Src/usb_fw_update.c
    Core/Src/usb_fw_update_main.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/usb_dc_hc32f460.c
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Src/syscalls.c
    $<TARGET_OBJECTS:hc32_startup_app>
)
set_target_properties(usb_fw_updater PROPERTIES SUFFIX .elf)
target_include_directories(usb_fw_updater PRIVATE Core/Inc)
target_compile_definitions(usb_fw_updater PRIVATE
    APP_AUTO_CONFIRM=$<BOOL:${APP_AUTO_CONFIRM}>
    APP_VERSION_MAJOR=${APP_VERSION_MAJOR_VALUE}
    APP_VERSION_MINOR=${APP_VERSION_MINOR_VALUE}
    APP_VERSION_REVISION=${APP_VERSION_REVISION_VALUE}
    APP_VERSION_BUILD=${APP_VERSION_BUILD_VALUE}
)
set_source_files_properties(
    Core/Src/usb_fw_update.c
    Core/Src/usb_fw_update_main.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wconversion;-Wshadow;-Werror"
)
target_include_directories(usb_fw_updater SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/components/cherryusb/common
    ${PROJECT_SOURCE_DIR}/components/cherryusb/core
)
target_link_libraries(usb_fw_updater PRIVATE
    cherryusb_device
    fw_update_mcuboot
    hc32_platform
    hc32_device
    mcuboot_bootutil
)
target_link_options(usb_fw_updater PRIVATE
    -T${PROJECT_SOURCE_DIR}/Linker/app.ld
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/usb_fw_updater.map
)
add_firmware_artifacts(usb_fw_updater)
add_mcuboot_signed_artifacts(usb_fw_updater updater)

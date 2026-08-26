cmake_minimum_required(VERSION 3.20)

foreach(required IN ITEMS
        HIL_OUTPUT_DIR
        HIL_BACKUP_BIN
        HIL_BOOT_BIN
        HIL_V1_PRIMARY_BIN
        HIL_V2_TEST_BIN
        HIL_V2_CONFIRM_BIN)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing -D${required}=...")
    endif()
endforeach()

if(NOT DEFINED HIL_SWD_SPEED_KHZ)
    set(HIL_SWD_SPEED_KHZ 4000)
endif()
if(NOT DEFINED HIL_BOOT_WAIT_MS)
    set(HIL_BOOT_WAIT_MS 30000)
endif()

foreach(path_var IN ITEMS
        HIL_OUTPUT_DIR
        HIL_BACKUP_BIN
        HIL_BOOT_BIN
        HIL_V1_PRIMARY_BIN
        HIL_V2_TEST_BIN
        HIL_V2_CONFIRM_BIN)
    get_filename_component(${path_var} "${${path_var}}" ABSOLUTE)
    file(TO_CMAKE_PATH "${${path_var}}" ${path_var})
endforeach()

file(MAKE_DIRECTORY "${HIL_OUTPUT_DIR}")

foreach(script IN ITEMS
        00_identify
        01_v1_install
        01_v1_recheck
        02_v2_test_boot
        03_v2_test_revert
        04_v2_confirm_boot
        05_v2_confirm_persist)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/templates/${script}.jlink.in"
        "${HIL_OUTPUT_DIR}/${script}.jlink"
        @ONLY
    )
endforeach()

message(STATUS "Rendered HIL commands under ${HIL_OUTPUT_DIR}")

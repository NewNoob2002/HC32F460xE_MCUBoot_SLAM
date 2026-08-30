if(NOT DEFINED HC32_PRODUCT_IDENTITY_FILE)
    set(HC32_PRODUCT_IDENTITY_FILE
        "${PROJECT_SOURCE_DIR}/Config/Product/ProductIdentity.env"
        CACHE FILEPATH "Public product and USB identity input"
    )
endif()
get_filename_component(HC32_PRODUCT_IDENTITY_FILE "${HC32_PRODUCT_IDENTITY_FILE}" ABSOLUTE)
if(NOT EXISTS "${HC32_PRODUCT_IDENTITY_FILE}")
    message(FATAL_ERROR "HC32_PRODUCT_IDENTITY_FILE does not exist: ${HC32_PRODUCT_IDENTITY_FILE}")
endif()

set(identity_required
    HC32_PRODUCT_CLASS
    HC32_HARDWARE_ID
    HC32_BOARD_ID
    HC32_BOARD_REVISION
    HC32_USB_VID
    HC32_USB_BOOT_PID
    HC32_USB_APPLICATION_PID
    HC32_USB_MANUFACTURER
    HC32_USB_BOOT_PRODUCT
    HC32_USB_APPLICATION_PRODUCT
    HC32_USB_SERIAL_PREFIX
)
set(identity_seen)
file(STRINGS "${HC32_PRODUCT_IDENTITY_FILE}" identity_lines)
foreach(identity_line IN LISTS identity_lines)
    if(identity_line MATCHES "^[ \t]*(#|$)")
        continue()
    endif()
    if(NOT identity_line MATCHES "^([A-Z0-9_]+)=(.*)$")
        message(FATAL_ERROR "Invalid product identity line: ${identity_line}")
    endif()
    set(identity_name "${CMAKE_MATCH_1}")
    set(identity_value "${CMAKE_MATCH_2}")
    list(FIND identity_required "${identity_name}" identity_index)
    list(FIND identity_seen "${identity_name}" identity_seen_index)
    if(identity_index EQUAL -1)
        message(FATAL_ERROR "Unknown product identity key: ${identity_name}")
    elseif(NOT identity_seen_index EQUAL -1)
        message(FATAL_ERROR "Duplicate product identity key: ${identity_name}")
    endif()
    list(APPEND identity_seen "${identity_name}")
    set(${identity_name} "${identity_value}")
endforeach()

foreach(identity_name IN LISTS identity_required)
    if(NOT DEFINED ${identity_name} OR "${${identity_name}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${identity_name} in ${HC32_PRODUCT_IDENTITY_FILE}")
    endif()
endforeach()

foreach(identity_name IN ITEMS HC32_HARDWARE_ID HC32_BOARD_ID HC32_BOARD_REVISION HC32_USB_VID
                               HC32_USB_BOOT_PID HC32_USB_APPLICATION_PID)
    if(NOT ${identity_name} MATCHES "^(0[xX][0-9A-Fa-f]+|[0-9]+)$")
        message(FATAL_ERROR "${identity_name} must be an unsigned integer")
    endif()
    math(EXPR ${identity_name}_VALUE "${${identity_name}}")
endforeach()
if(HC32_HARDWARE_ID_VALUE GREATER 4294967295 OR HC32_BOARD_ID_VALUE GREATER 4294967295
   OR HC32_BOARD_REVISION_VALUE GREATER 65535
   OR HC32_USB_VID_VALUE LESS 1 OR HC32_USB_VID_VALUE GREATER 65535
   OR HC32_USB_BOOT_PID_VALUE LESS 1 OR HC32_USB_BOOT_PID_VALUE GREATER 65535
   OR HC32_USB_APPLICATION_PID_VALUE LESS 1 OR HC32_USB_APPLICATION_PID_VALUE GREATER 65535
   OR HC32_USB_BOOT_PID_VALUE EQUAL HC32_USB_APPLICATION_PID_VALUE)
    message(FATAL_ERROR "Product identity numeric field exceeds its protocol width")
endif()

if(NOT DEFINED HC32_PRODUCT_RELEASE)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(HC32_PRODUCT_RELEASE ON)
    else()
        set(HC32_PRODUCT_RELEASE OFF)
    endif()
endif()
if(HC32_PRODUCT_RELEASE)
    if(NOT HC32_PRODUCT_CLASS STREQUAL "production")
        message(FATAL_ERROR
            "Release requires HC32_PRODUCT_CLASS=production")
    endif()
endif()

function(_hc32_append_le_hex output value byte_count)
    set(result "${${output}}")
    math(EXPR last_byte "${byte_count} - 1")
    foreach(byte RANGE 0 ${last_byte})
        math(EXPR shift "${byte} * 8")
        math(EXPR high "(${value} >> ${shift} >> 4) & 15")
        math(EXPR low "(${value} >> ${shift}) & 15")
        string(SUBSTRING "0123456789abcdef" ${high} 1 high_char)
        string(SUBSTRING "0123456789abcdef" ${low} 1 low_char)
        string(APPEND result "${high_char}${low_char}")
    endforeach()
    set(${output} "${result}" PARENT_SCOPE)
endfunction()

set(HC32_COMPATIBILITY_TLV_HEX "0100")
_hc32_append_le_hex(HC32_COMPATIBILITY_TLV_HEX ${HC32_BOARD_REVISION_VALUE} 2)
_hc32_append_le_hex(HC32_COMPATIBILITY_TLV_HEX ${HC32_HARDWARE_ID_VALUE} 4)
_hc32_append_le_hex(HC32_COMPATIBILITY_TLV_HEX ${HC32_BOARD_ID_VALUE} 4)

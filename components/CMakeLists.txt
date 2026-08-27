set(MCUBOOT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/mcuboot-2.4.0)
set(TINYCRYPT_ROOT ${MCUBOOT_ROOT}/ext/tinycrypt/lib)
set(MCUBOOT_ASN1_ROOT ${MCUBOOT_ROOT}/ext/mbedtls-asn1)
set(BOOTUTIL_ROOT ${MCUBOOT_ROOT}/boot/bootutil)

add_subdirectory(mcuboot_port)
add_subdirectory(cherryusb)

add_library(tinycrypt STATIC
    ${TINYCRYPT_ROOT}/source/sha256.c
    ${TINYCRYPT_ROOT}/source/utils.c
    ${TINYCRYPT_ROOT}/source/ecc.c
    ${TINYCRYPT_ROOT}/source/ecc_dsa.c
    ${TINYCRYPT_ROOT}/source/ecc_platform_specific.c
)
target_include_directories(tinycrypt PUBLIC ${TINYCRYPT_ROOT}/include)
target_link_libraries(tinycrypt PRIVATE hc32_project_options)

add_library(mcuboot_asn1 STATIC
    ${MCUBOOT_ASN1_ROOT}/src/asn1parse.c
    ${MCUBOOT_ASN1_ROOT}/src/platform_util.c
)
target_include_directories(mcuboot_asn1 PUBLIC ${MCUBOOT_ASN1_ROOT}/include)
target_link_libraries(mcuboot_asn1 PRIVATE hc32_project_options)

add_library(mcuboot_bootutil STATIC
    ${BOOTUTIL_ROOT}/src/bootutil_area.c
    ${BOOTUTIL_ROOT}/src/bootutil_find_key.c
    ${BOOTUTIL_ROOT}/src/bootutil_img_hash.c
    ${BOOTUTIL_ROOT}/src/bootutil_img_security_cnt.c
    ${BOOTUTIL_ROOT}/src/bootutil_loader.c
    ${BOOTUTIL_ROOT}/src/bootutil_misc.c
    ${BOOTUTIL_ROOT}/src/bootutil_public.c
    ${BOOTUTIL_ROOT}/src/caps.c
    ${BOOTUTIL_ROOT}/src/fault_injection_hardening.c
    ${BOOTUTIL_ROOT}/src/image_ecdsa.c
    ${BOOTUTIL_ROOT}/src/image_validate.c
    ${BOOTUTIL_ROOT}/src/loader.c
    ${BOOTUTIL_ROOT}/src/swap_misc.c
    ${BOOTUTIL_ROOT}/src/swap_scratch.c
    ${BOOTUTIL_ROOT}/src/tlv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mcuboot_port/Src/config_checks.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mcuboot_port/Src/keys.c
    ${MCUBOOT_PUBLIC_KEY_SOURCE}
)
target_include_directories(mcuboot_bootutil
    PUBLIC
        ${BOOTUTIL_ROOT}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/mcuboot_port/Inc
    PRIVATE
        ${BOOTUTIL_ROOT}/src
)
target_link_libraries(mcuboot_bootutil
    PUBLIC mcuboot_port_hc32
    PRIVATE tinycrypt mcuboot_asn1 hc32_project_options
)
target_compile_options(mcuboot_bootutil PRIVATE $<$<CONFIG:Release>:-UNDEBUG>)

add_subdirectory(fw_update)

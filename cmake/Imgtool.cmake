set(MCUBOOT_IMGTOOL "${PROJECT_SOURCE_DIR}/components/mcuboot-2.4.0/scripts/imgtool.py")
set(MCUBOOT_SIGNING_KEY "" CACHE FILEPATH "ECDSA-P256 private key used to verify MCUboot images")
set(MCUBOOT_GENERATED_DEBUG_KEY OFF)

if(EXISTS "${PROJECT_SOURCE_DIR}/.venv/bin/python")
    set(MCUBOOT_PYTHON "${PROJECT_SOURCE_DIR}/.venv/bin/python")
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(MCUBOOT_PYTHON "${Python3_EXECUTABLE}")
endif()

if(MCUBOOT_SIGNING_KEY)
    get_filename_component(MCUBOOT_ACTIVE_SIGNING_KEY "${MCUBOOT_SIGNING_KEY}" ABSOLUTE)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Release builds require -DMCUBOOT_SIGNING_KEY=/path/to/ec-p256.pem")
else()
    set(MCUBOOT_ACTIVE_SIGNING_KEY "${CMAKE_BINARY_DIR}/generated/keys/dev-ec-p256.pem")
    set(MCUBOOT_GENERATED_DEBUG_KEY ON)
endif()

if(NOT EXISTS "${MCUBOOT_ACTIVE_SIGNING_KEY}")
    if(MCUBOOT_SIGNING_KEY)
        message(FATAL_ERROR "MCUBOOT_SIGNING_KEY does not exist: ${MCUBOOT_ACTIVE_SIGNING_KEY}")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/keys")
    execute_process(
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" keygen
                -t ecdsa-p256 -k "${MCUBOOT_ACTIVE_SIGNING_KEY}"
        RESULT_VARIABLE keygen_result
    )
    if(NOT keygen_result EQUAL 0)
        message(FATAL_ERROR "imgtool failed to generate the Debug signing key")
    endif()
endif()

if(MCUBOOT_GENERATED_DEBUG_KEY)
    file(CHMOD "${MCUBOOT_ACTIVE_SIGNING_KEY}" PERMISSIONS OWNER_READ OWNER_WRITE)
endif()
file(READ "${MCUBOOT_ACTIVE_SIGNING_KEY}" signing_key_text LIMIT 256)
string(FIND "${signing_key_text}" "PRIVATE KEY" private_key_marker)
if(private_key_marker EQUAL -1)
    message(FATAL_ERROR "MCUBOOT_SIGNING_KEY must contain a private key")
endif()

set(MCUBOOT_PUBLIC_KEY_SOURCE "${CMAKE_BINARY_DIR}/generated/keys/signing_keys.c")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/keys")
execute_process(
    COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" getpub
            -k "${MCUBOOT_ACTIVE_SIGNING_KEY}" -e lang-c -o "${MCUBOOT_PUBLIC_KEY_SOURCE}"
    RESULT_VARIABLE getpub_result
)
if(NOT getpub_result EQUAL 0)
    message(FATAL_ERROR "imgtool failed to export the MCUboot public key")
endif()
file(READ "${MCUBOOT_PUBLIC_KEY_SOURCE}" public_key_source)
string(FIND "${public_key_source}" "ecdsa_pub_key[]" ecdsa_key_symbol)
if(ecdsa_key_symbol EQUAL -1)
    message(FATAL_ERROR "MCUBOOT_SIGNING_KEY must be an ECDSA-P256 key")
endif()

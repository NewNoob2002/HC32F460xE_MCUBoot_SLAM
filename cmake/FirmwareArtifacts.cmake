function(add_firmware_artifacts target)
    set(hex_file "${CMAKE_CURRENT_BINARY_DIR}/${target}.hex")
    set(bin_file "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin")
    add_custom_command(TARGET ${target} POST_BUILD
        BYPRODUCTS "${hex_file}" "${bin_file}"
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${target}> "${hex_file}"
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> "${bin_file}"
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        VERBATIM
    )
endfunction()

function(add_mcuboot_signed_artifacts target)
    set(artifact_name "app")
    if(ARGC GREATER 1)
        set(artifact_name "${ARGV1}")
    endif()
    set(artifact_dir "${CMAKE_BINARY_DIR}/artifacts")
    set(raw_image "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin")
    set(signed_image "${artifact_dir}/${artifact_name}_signed.bin")
    set(primary_image "${artifact_dir}/${artifact_name}_primary.bin")
    set(update_image "${artifact_dir}/${artifact_name}_update.bin")
    set(verify_stamp "${artifact_dir}/verify_${artifact_name}_image.stamp")
    set(sign_args
        sign
        --align "${FLASH_WRITE_ALIGN}"
        --max-sectors "${MCUBOOT_MAX_IMG_SECTORS}"
        --version "${APP_VERSION}"
        --header-size "${MCUBOOT_HEADER_SIZE}"
        --slot-size "${PRIMARY_SLOT_SIZE}"
        --pad-header
        --erased-val 0xff
        --key "${MCUBOOT_ACTIVE_SIGNING_KEY}"
        --custom-tlv 0x00A0 "0x${HC32_COMPATIBILITY_TLV_HEX}"
    )

    file(MAKE_DIRECTORY "${artifact_dir}")

    add_custom_command(OUTPUT "${signed_image}"
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" ${sign_args}
                "${raw_image}" "${signed_image}"
        DEPENDS "${raw_image}" "${MCUBOOT_ACTIVE_SIGNING_KEY}" "${MCUBOOT_IMGTOOL}"
                "${HC32_PRODUCT_IDENTITY_FILE}"
        VERBATIM
    )
    add_custom_command(OUTPUT "${primary_image}"
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" ${sign_args}
                --pad --confirm "${raw_image}" "${primary_image}"
        DEPENDS "${raw_image}" "${MCUBOOT_ACTIVE_SIGNING_KEY}" "${MCUBOOT_IMGTOOL}"
                "${HC32_PRODUCT_IDENTITY_FILE}"
        VERBATIM
    )
    add_custom_command(OUTPUT "${update_image}"
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" ${sign_args}
                --pad --test "${raw_image}" "${update_image}"
        DEPENDS "${raw_image}" "${MCUBOOT_ACTIVE_SIGNING_KEY}" "${MCUBOOT_IMGTOOL}"
                "${HC32_PRODUCT_IDENTITY_FILE}"
        VERBATIM
    )

    add_custom_target(${artifact_name}_signed DEPENDS "${signed_image}")
    add_custom_target(${artifact_name}_primary DEPENDS "${primary_image}")
    add_custom_target(${artifact_name}_update DEPENDS "${update_image}")

    add_custom_command(OUTPUT "${verify_stamp}"
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" verify
                -k "${MCUBOOT_ACTIVE_SIGNING_KEY}" "${signed_image}"
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" verify
                -k "${MCUBOOT_ACTIVE_SIGNING_KEY}" "${primary_image}"
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" verify
                -k "${MCUBOOT_ACTIVE_SIGNING_KEY}" "${update_image}"
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" dumpinfo "${primary_image}"
        COMMAND "${MCUBOOT_PYTHON}" "${MCUBOOT_IMGTOOL}" dumpinfo "${update_image}"
        COMMAND "${MCUBOOT_PYTHON}" -c
                "__import__('sys').exit(any(__import__('os').path.getsize(p) != int(__import__('sys').argv[1], 0) for p in __import__('sys').argv[2:]))"
                "${PRIMARY_SLOT_SIZE}" "${primary_image}" "${update_image}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${verify_stamp}"
        DEPENDS "${signed_image}" "${primary_image}" "${update_image}"
                "${MCUBOOT_ACTIVE_SIGNING_KEY}" "${MCUBOOT_IMGTOOL}"
        VERBATIM
    )
    add_custom_target(verify_${artifact_name}_image DEPENDS "${verify_stamp}")
endfunction()

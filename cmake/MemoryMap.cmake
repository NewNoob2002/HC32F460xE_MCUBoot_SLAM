set(FLASH_TOTAL_SIZE        0x00080000)
set(FLASH_SECTOR_SIZE       0x00002000)
set(FLASH_WRITE_ALIGN       4)
set(BOOT_FLASH_BASE         0x00000000)
set(BOOT_FLASH_SIZE         0x00010000)
set(PRIMARY_SLOT_BASE       0x00010000)
set(PRIMARY_SLOT_SIZE       0x00032000)
set(SECONDARY_SLOT_BASE     0x00042000)
set(SECONDARY_SLOT_SIZE     0x00032000)
set(SCRATCH_BASE            0x00074000)
set(SCRATCH_SIZE            0x00002000)
set(RESERVED_BASE           0x00076000)
set(RESERVED_SIZE           0x0000A000)
set(MCUBOOT_HEADER_SIZE     0x00000200)
set(MCUBOOT_TLV_RESERVE     0x00000400)
set(MCUBOOT_TRAILER_RESERVE 0x00002000)
set(APP_LINK_ORIGIN         0x00010200)
set(APP_LINK_SIZE           0x0002FA00)
set(MCUBOOT_MAX_IMG_SECTORS 25)

math(EXPR expected_primary   "${BOOT_FLASH_BASE} + ${BOOT_FLASH_SIZE}")
math(EXPR expected_secondary "${PRIMARY_SLOT_BASE} + ${PRIMARY_SLOT_SIZE}")
math(EXPR expected_scratch   "${SECONDARY_SLOT_BASE} + ${SECONDARY_SLOT_SIZE}")
math(EXPR expected_reserved  "${SCRATCH_BASE} + ${SCRATCH_SIZE}")
math(EXPR expected_end       "${RESERVED_BASE} + ${RESERVED_SIZE}")
math(EXPR expected_app       "${PRIMARY_SLOT_BASE} + ${MCUBOOT_HEADER_SIZE}")
math(EXPR expected_app_size  "${PRIMARY_SLOT_SIZE} - ${MCUBOOT_HEADER_SIZE} - ${MCUBOOT_TLV_RESERVE} - ${MCUBOOT_TRAILER_RESERVE}")
math(EXPR minimum_reserved_size "${FLASH_SECTOR_SIZE} * 2")

if(NOT PRIMARY_SLOT_BASE EQUAL expected_primary OR
   NOT SECONDARY_SLOT_BASE EQUAL expected_secondary OR
   NOT SCRATCH_BASE EQUAL expected_scratch OR
   NOT RESERVED_BASE EQUAL expected_reserved OR
   NOT FLASH_TOTAL_SIZE EQUAL expected_end OR
   NOT APP_LINK_ORIGIN EQUAL expected_app OR
   NOT APP_LINK_SIZE EQUAL expected_app_size OR
   RESERVED_SIZE LESS minimum_reserved_size)
    message(FATAL_ERROR "Invalid MCUboot flash layout")
endif()

foreach(value IN ITEMS BOOT_FLASH_BASE BOOT_FLASH_SIZE PRIMARY_SLOT_BASE PRIMARY_SLOT_SIZE
                       SECONDARY_SLOT_BASE SECONDARY_SLOT_SIZE SCRATCH_BASE SCRATCH_SIZE
                       RESERVED_BASE RESERVED_SIZE)
    math(EXPR remainder "${${value}} % ${FLASH_SECTOR_SIZE}")
    if(NOT remainder EQUAL 0)
        message(FATAL_ERROR "${value} must be ${FLASH_SECTOR_SIZE}-byte aligned")
    endif()
endforeach()

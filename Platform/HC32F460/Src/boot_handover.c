#include "boot_handover.h"

#include "boot_memory_map.h"
#include "flash_map_backend/flash_map_backend.h"

#define HC32_MAIN_SRAM_BASE  UINT32_C(0x1FFF8000)
#define HC32_MAIN_SRAM_LIMIT UINT32_C(0x20027000)

_Static_assert(MCUBOOT_HEADER_SIZE <= UINT16_MAX, "MCUboot header size exceeds boot response width");
_Static_assert(APP_LINK_ORIGIN == PRIMARY_SLOT_BASE + MCUBOOT_HEADER_SIZE, "Application origin mismatch");

bool boot_handover_image_is_valid(uint8_t flash_device_id, uint32_t image_offset, uint16_t header_size) {
    return flash_device_id == FLASH_DEVICE_INTERNAL_FLASH && image_offset == PRIMARY_SLOT_BASE
           && header_size == MCUBOOT_HEADER_SIZE;
}

bool boot_handover_vectors_are_valid(uint32_t stack_pointer, uint32_t reset_vector) {
    const uint32_t reset_address = reset_vector & ~UINT32_C(1);
    const uint32_t app_end = APP_LINK_ORIGIN + APP_LINK_SIZE;

    return stack_pointer > HC32_MAIN_SRAM_BASE && stack_pointer <= HC32_MAIN_SRAM_LIMIT
           && (stack_pointer & UINT32_C(7)) == 0U && (reset_vector & UINT32_C(1)) != 0U
           && reset_address >= APP_LINK_ORIGIN && reset_address < app_end;
}

#ifndef BOOT_HOST_TEST

#include <stddef.h>

#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bsp_panic.h"
#include "hc32f460.h"

_Static_assert(HC32_MAIN_SRAM_BASE == SRAM_BASE, "HC32 SRAM base mismatch");
_Static_assert((APP_LINK_ORIGIN & ~SCB_VTOR_TBLOFF_Msk) == 0U, "Application vector table is misaligned");

_Noreturn void boot_handover_jump(uint32_t stack_pointer, uint32_t reset_vector);

_Noreturn void boot_handover(const struct boot_rsp* rsp) {
    if (rsp == NULL || rsp->br_hdr == NULL)
        bsp_panic("invalid boot response");
    if (!boot_handover_image_is_valid(rsp->br_flash_dev_id, rsp->br_image_off, rsp->br_hdr->ih_hdr_size))
        bsp_panic("invalid boot image location");

    const uint32_t vector_address = APP_LINK_ORIGIN;
    const uint32_t* vectors = (const uint32_t*)(uintptr_t)vector_address;
    const uint32_t stack_pointer = vectors[0];
    const uint32_t reset_vector = vectors[1];

    if (!boot_handover_vectors_are_valid(stack_pointer, reset_vector))
        bsp_panic("invalid application vectors");

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    for (size_t i = 0U; i < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); ++i) {
        NVIC->ICER[i] = UINT32_MAX;
        NVIC->ICPR[i] = UINT32_MAX;
    }

    SCB->VTOR = vector_address;
    __DSB();
    __ISB();
    boot_handover_jump(stack_pointer, reset_vector);
}

#endif

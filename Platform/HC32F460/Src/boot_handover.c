#include "boot_handover.h"

uint32_t boot_handover_vector_address(uint32_t image_offset, uint16_t header_size) {
    return image_offset + (uint32_t)header_size;
}

#ifndef BOOT_HOST_TEST

#include <stddef.h>

#include "boot_memory_map.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "hc32f460.h"

#define HC32_SRAM_END 0x20027FFFUL

static _Noreturn void halt(void) {
    __disable_irq();
    for (;;) {
        __WFI();
    }
}

_Noreturn void boot_handover(const struct boot_rsp *rsp) {
    if (rsp == NULL || rsp->br_hdr == NULL)
        halt();

    const uint32_t vector_address =
        boot_handover_vector_address(rsp->br_image_off, rsp->br_hdr->ih_hdr_size);
    const uint32_t app_end = APP_LINK_ORIGIN + APP_LINK_SIZE;

    if (vector_address < APP_LINK_ORIGIN || vector_address < rsp->br_image_off
        || (vector_address & ~SCB_VTOR_TBLOFF_Msk) != 0U
        || vector_address > app_end - (2U * sizeof(uint32_t)))
        halt();

    const uint32_t *vectors = (const uint32_t *)(uintptr_t)vector_address;
    const uint32_t stack_pointer = vectors[0];
    const uint32_t reset_vector = vectors[1];
    const uint32_t reset_address = reset_vector & ~1UL;

    if (stack_pointer < SRAM_BASE || stack_pointer > HC32_SRAM_END
        || (stack_pointer & 7U) != 0U || (reset_vector & 1U) == 0U
        || reset_address < APP_LINK_ORIGIN || reset_address >= app_end)
        halt();

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
    __set_MSP(stack_pointer);
    ((void (*)(void))(uintptr_t)reset_vector)();
    halt();
}

#endif

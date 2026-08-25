#include "hc32f460_it.h"
#include "hc32f460.h"
#include <stdbool.h>

volatile boot_fault_snapshot_t g_boot_fault_snapshot __attribute__((section(".noinit.boot_fault"), used));

void NMI_Handler(void) {
    while (1) {
        __NOP();
    }
}

__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b boot_hardfault_capture\n");
}

__attribute__((noreturn)) void boot_hardfault_capture(uint32_t* frame, uint32_t exc_return) {
    __disable_irq();
    if ((exc_return & (1UL << 4U)) == 0U)
        frame += 18U; /* Skip S0-S15, FPSCR, and the reserved word. */
    g_boot_fault_snapshot.magic = BOOT_FAULT_SNAPSHOT_MAGIC;
    g_boot_fault_snapshot.exc_return = exc_return;
    g_boot_fault_snapshot.msp = __get_MSP();
    g_boot_fault_snapshot.psp = __get_PSP();
    g_boot_fault_snapshot.frame_address = (uint32_t)frame;
    uint32_t frame_address = (uint32_t)frame;
    bool frame_valid = ((frame_address >= 0x1FFF8000UL) && (frame_address <= (0x20027000UL - 32UL))) ||
                       ((frame_address >= 0x200F0000UL) && (frame_address <= (0x200F1000UL - 32UL)));
    g_boot_fault_snapshot.frame_valid = frame_valid ? 1UL : 0UL;
    if (frame_valid) {
        g_boot_fault_snapshot.stacked_r0 = frame[0];
        g_boot_fault_snapshot.stacked_r1 = frame[1];
        g_boot_fault_snapshot.stacked_r2 = frame[2];
        g_boot_fault_snapshot.stacked_r3 = frame[3];
        g_boot_fault_snapshot.stacked_r12 = frame[4];
        g_boot_fault_snapshot.stacked_lr = frame[5];
        g_boot_fault_snapshot.stacked_pc = frame[6];
        g_boot_fault_snapshot.stacked_xpsr = frame[7];
    } else {
        g_boot_fault_snapshot.stacked_r0 = 0xFFFFFFFFUL;
        g_boot_fault_snapshot.stacked_r1 = 0xFFFFFFFFUL;
        g_boot_fault_snapshot.stacked_r2 = 0xFFFFFFFFUL;
        g_boot_fault_snapshot.stacked_r3 = 0xFFFFFFFFUL;
        g_boot_fault_snapshot.stacked_r12 = 0xFFFFFFFFUL;
        g_boot_fault_snapshot.stacked_lr = 0xFFFFFFFFUL;
        g_boot_fault_snapshot.stacked_pc = 0xFFFFFFFFUL;
        g_boot_fault_snapshot.stacked_xpsr = 0xFFFFFFFFUL;
    }
    g_boot_fault_snapshot.cfsr = SCB->CFSR;
    g_boot_fault_snapshot.hfsr = SCB->HFSR;
    g_boot_fault_snapshot.dfsr = SCB->DFSR;
    g_boot_fault_snapshot.afsr = SCB->AFSR;
    g_boot_fault_snapshot.mmfar = SCB->MMFAR;
    g_boot_fault_snapshot.bfar = SCB->BFAR;
    g_boot_fault_snapshot.shcsr = SCB->SHCSR;
    g_boot_fault_snapshot.cpacr = SCB->CPACR;
    g_boot_fault_snapshot.control = __get_CONTROL();
    g_boot_fault_snapshot.primask = __get_PRIMASK();
    g_boot_fault_snapshot.basepri = __get_BASEPRI();
    g_boot_fault_snapshot.faultmask = __get_FAULTMASK();
    g_boot_fault_snapshot.icsr = SCB->ICSR;
    g_boot_fault_snapshot.vtor = SCB->VTOR;
    __DSB();
    __ISB();
    __BKPT(0);
    while (1) {
        __NOP();
    }
}

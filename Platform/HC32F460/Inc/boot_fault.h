#ifndef BOOT_FAULT_H
#define BOOT_FAULT_H
#include <stdint.h>

#define BOOT_FAULT_SNAPSHOT_MAGIC 0x4641554CUL

typedef struct {
    uint32_t magic;
    uint32_t exc_return;
    uint32_t msp;
    uint32_t psp;
    uint32_t frame_address;
    uint32_t frame_valid;
    uint32_t stacked_r0;
    uint32_t stacked_r1;
    uint32_t stacked_r2;
    uint32_t stacked_r3;
    uint32_t stacked_r12;
    uint32_t stacked_lr;
    uint32_t stacked_pc;
    uint32_t stacked_xpsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t shcsr;
    uint32_t cpacr;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
    uint32_t icsr;
    uint32_t vtor;
} boot_fault_snapshot_t;

extern volatile boot_fault_snapshot_t g_boot_fault_snapshot;
void NMI_Handler(void);
void HardFault_Handler(void);
void boot_hardfault_capture(uint32_t* stack_frame, uint32_t exc_return);
#endif

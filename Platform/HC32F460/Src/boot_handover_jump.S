    .syntax unified
    .cpu cortex-m4
    .thumb

    .section .text.boot_handover_jump, "ax", %progbits
    .align 2
    .global boot_handover_jump
    .type boot_handover_jump, %function
    .thumb_func
boot_handover_jump:
    movs r2, #0
    msr basepri, r2
    msr faultmask, r2
    msr control, r2
    isb
    msr msp, r0
    cpsie i
    bx r1
    .size boot_handover_jump, . - boot_handover_jump

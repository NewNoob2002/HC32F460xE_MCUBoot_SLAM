#ifndef BOOT_HANDOVER_H
#define BOOT_HANDOVER_H

#include <stdint.h>

struct boot_rsp;

uint32_t boot_handover_vector_address(uint32_t image_offset, uint16_t header_size);
_Noreturn void boot_handover(const struct boot_rsp *rsp);

#endif

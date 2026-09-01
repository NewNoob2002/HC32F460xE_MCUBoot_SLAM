#ifndef BOOT_HANDOVER_H
#define BOOT_HANDOVER_H

#include <stdbool.h>
#include <stdint.h>

struct boot_rsp;

bool boot_handover_image_is_valid(uint8_t flash_device_id, uint32_t image_offset, uint16_t header_size);
bool boot_handover_vectors_are_valid(uint32_t stack_pointer, uint32_t reset_vector);
_Noreturn void boot_handover(const struct boot_rsp* rsp);

#endif

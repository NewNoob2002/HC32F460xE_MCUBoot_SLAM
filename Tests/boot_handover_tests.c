#include <assert.h>

#include "boot_handover.h"

int main(void) {
    assert(boot_handover_vector_address(0x10000UL, 0x200U) == 0x10200UL);
    assert(boot_handover_vector_address(0x10000UL, 0U) == 0x10000UL);
    return 0;
}

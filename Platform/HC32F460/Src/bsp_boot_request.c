#include "bsp_boot_request.h"

#include "bsp_reset.h"

#define BOOT_REQUEST_MAGIC    UINT32_C(0x52435652) /* "RVCR" */
#define BOOT_REQUEST_RECOVERY UINT32_C(0x00000001)

struct boot_request {
    uint32_t magic;
    uint32_t request;
    uint32_t request_inverse;
    uint32_t reserved;
};

/* Both linker scripts place .boot_request at the same fixed retention-SRAM address and never zero it. */
static volatile struct boot_request boot_request __attribute__((section(".boot_request"), used));

static void boot_request_clear(void) {
    boot_request.magic = 0U;
    boot_request.request = 0U;
    boot_request.request_inverse = 0U;
    boot_request.reserved = 0U;
}

void bsp_boot_request_recovery(void) {
    boot_request.request = BOOT_REQUEST_RECOVERY;
    boot_request.request_inverse = ~BOOT_REQUEST_RECOVERY;
    boot_request.reserved = 0U;
    boot_request.magic = BOOT_REQUEST_MAGIC;
}

bool bsp_boot_request_take_recovery(uint32_t reset_flags) {
    /* Power-on SRAM contents are undefined; only a software reset can carry a request written by the Application. */
    const bool requested = bsp_reset_was_software(reset_flags) && boot_request.magic == BOOT_REQUEST_MAGIC
                           && boot_request.request == BOOT_REQUEST_RECOVERY
                           && boot_request.request_inverse == ~BOOT_REQUEST_RECOVERY;
    boot_request_clear();
    return requested;
}

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_i2c2.h"
#include "husb238.h"
#include "mp2762a.h"
#include "power_devices.h"

static uint8_t g_probe_addresses[4];
static unsigned int g_probe_count;
static unsigned int g_identity_reads;
static int g_alternate_probe_result;

int bsp_i2c2_init(void) {
    return BSP_I2C2_OK;
}

int bsp_i2c2_probe(uint8_t address) {
    assert(g_probe_count < 4U);
    g_probe_addresses[g_probe_count++] = address;
    return address == BQ40Z50_I2C_ADDRESS_ALTERNATE ? g_alternate_probe_result : BSP_I2C2_OK;
}

int bq40z50_read_identity(uint8_t address, struct bq40z50_identity* identity) {
    assert(address == BQ40Z50_I2C_ADDRESS_ALTERNATE);
    assert(identity != NULL);
    ++g_identity_reads;
    identity->device_type = 0x4500U;
    return BQ40Z50_OK;
}

static void reset_fake(void) {
    g_probe_count = 0U;
    g_identity_reads = 0U;
    g_alternate_probe_result = BSP_I2C2_OK;
}

int main(void) {
    struct power_devices_status status;

    reset_fake();
    power_devices_init(&status);
    assert(g_probe_count == 4U);
    assert(g_probe_addresses[0] == BQ40Z50_I2C_ADDRESS_DEFAULT);
    assert(g_probe_addresses[1] == BQ40Z50_I2C_ADDRESS_ALTERNATE);
    assert(g_probe_addresses[2] == HUSB238_I2C_ADDRESS);
    assert(g_probe_addresses[3] == MP2762A_I2C_ADDRESS);
    assert(g_identity_reads == 1U);
    assert(status.bq_alternate_identity.device_type == 0x4500U);

    reset_fake();
    g_alternate_probe_result = BSP_I2C2_ERR_ADDR_NACK;
    power_devices_init(&status);
    assert(g_identity_reads == 0U);
    assert(status.bq_alternate_identity_result == BSP_I2C2_ERR_ADDR_NACK);

    power_devices_init(NULL);
    return 0;
}

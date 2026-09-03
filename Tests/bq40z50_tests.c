#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bq40z50.h"
#include "bsp_i2c2.h"

static uint16_t g_command;
static unsigned int g_reads;
static int g_bus_result;
static bool g_bad_count;
static uint8_t g_expected_address;

int bsp_i2c2_write(uint8_t address, const uint8_t* data, uint32_t length) {
    assert(address == g_expected_address);
    assert(data != NULL);
    assert(length == 3U);
    assert(data[0] == 0x00U);
    g_command = (uint16_t)data[1] | ((uint16_t)data[2] << 8U);
    return g_bus_result;
}

int bsp_i2c2_read_reg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) {
    static const uint8_t firmware[] = {0x45U, 0x00U, 0x01U, 0x06U, 0x00U, 0x24U, 0x00U, 0x03U, 0x85U, 0x02U, 0x00U};
    const uint8_t* payload = NULL;
    uint8_t word[2];
    uint8_t payload_length = 0U;

    assert(address == g_expected_address);
    assert(reg == 0x23U);
    assert(data != NULL);
    if (g_bus_result != BSP_I2C2_OK)
        return g_bus_result;
    switch (g_command) {
        case 0x0001U:
            word[0] = 0x00U;
            word[1] = 0x45U;
            payload = word;
            payload_length = 2U;
            break;
        case 0x0002U:
            payload = firmware;
            payload_length = sizeof(firmware);
            break;
        case 0x0003U:
            word[0] = 0x0CU;
            word[1] = 0x00U;
            payload = word;
            payload_length = 2U;
            break;
        case 0x0006U:
            word[0] = 0x07U;
            word[1] = 0x21U;
            payload = word;
            payload_length = 2U;
            break;
        default:
            assert(false);
    }
    assert(length == (uint32_t)payload_length + 1U);
    data[0] = g_bad_count ? 0U : payload_length;
    memcpy(&data[1], payload, payload_length);
    g_reads++;
    return BSP_I2C2_OK;
}

static void reset_fake(void) {
    g_command = 0U;
    g_reads = 0U;
    g_bus_result = BSP_I2C2_OK;
    g_bad_count = false;
    g_expected_address = BQ40Z50_I2C_ADDRESS_ALTERNATE;
}

int main(void) {
    struct bq40z50_identity identity;

    reset_fake();
    assert(bq40z50_read_identity(g_expected_address, &identity) == BQ40Z50_OK);
    assert(g_reads == 4U);
    assert(identity.device_type == 0x4500U);
    assert(identity.firmware.device_number == 0x4500U);
    assert(identity.firmware.version == 0x0106U);
    assert(identity.firmware.build == 0x0024U);
    assert(identity.firmware.firmware_type == 0x00U);
    assert(identity.firmware.impedance_track_version == 0x0385U);
    assert(identity.firmware.reserved == 0x02U);
    assert(identity.firmware.execution_context == 0x00U);
    assert(identity.hardware_version == 0x000CU);
    assert(identity.chemistry_id == 0x2107U);

    reset_fake();
    g_bad_count = true;
    assert(bq40z50_read_identity(g_expected_address, &identity) == BQ40Z50_ERR_PROTOCOL);

    reset_fake();
    g_bus_result = BSP_I2C2_ERR_TX_TIMEOUT;
    assert(bq40z50_read_identity(g_expected_address, &identity) == BSP_I2C2_ERR_TX_TIMEOUT);
    assert(bq40z50_read_identity(g_expected_address, NULL) == BQ40Z50_ERR_ARGUMENT);
    assert(bq40z50_read_identity(0x80U, &identity) == BQ40Z50_ERR_ARGUMENT);
    return 0;
}

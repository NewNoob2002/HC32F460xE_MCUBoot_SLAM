#ifndef BSP_I2C2_H
#define BSP_I2C2_H

#include <stdint.h>

enum bsp_i2c2_result {
    BSP_I2C2_OK = 0,
    BSP_I2C2_ERR_INIT = -1,
    BSP_I2C2_ERR_ARGUMENT = -2,
    BSP_I2C2_ERR_NOT_READY = -3,
    BSP_I2C2_ERR_INIT_DEINIT = -4,
    BSP_I2C2_ERR_INIT_DEFAULTS = -5,
    BSP_I2C2_ERR_BUS_BUSY = -6,
    BSP_I2C2_ERR_START_TIMEOUT = -7,
    BSP_I2C2_ERR_ADDR_NACK = -8,
    BSP_I2C2_ERR_ADDR_TIMEOUT = -9,
    BSP_I2C2_ERR_TX_NACK = -10,
    BSP_I2C2_ERR_TX_TIMEOUT = -11,
    BSP_I2C2_ERR_RESTART_TIMEOUT = -12,
    BSP_I2C2_ERR_RX_ADDR_NACK = -13,
    BSP_I2C2_ERR_RX_ADDR_TIMEOUT = -14,
    BSP_I2C2_ERR_RX_STOP_TIMEOUT = -15,
    BSP_I2C2_ERR_STOP_TIMEOUT = -18,
    BSP_I2C2_ERR_ARBITRATION_LOST = -19,
};

/* Requires write protection to be unlocked by the caller. */
int bsp_i2c2_init(void);
int bsp_i2c2_probe(uint8_t address);
int bsp_i2c2_write(uint8_t address, const uint8_t* data, uint32_t length);
int bsp_i2c2_read_reg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length);

#endif

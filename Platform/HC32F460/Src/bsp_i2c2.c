#include "bsp_i2c2.h"

#include <stdbool.h>
#include <stddef.h>

#include "bsp_board_config.h"
#include "bsp_timebase.h"
#include "hc32_ll.h"

/* LL wait loops count iterations, so a fixed count lasts 4x longer at 50 MHz than at 200 MHz. Scale it with
 * SYSCLK so every wait lasts at least ~30 ms (one iteration takes >= 4 cycles): above the 25 ms SMBus per-message
 * clock-stretch limit (tLOW:SEXT) that the BQ40Z50 may use. */
#define BSP_I2C2_WAIT_MS              30UL
#define BSP_I2C2_WAIT_LOOP_CYCLES     4UL
#define BSP_I2C2_TIMEOUT              (SystemCoreClock / 1000UL * BSP_I2C2_WAIT_MS / BSP_I2C2_WAIT_LOOP_CYCLES)
/* A stuck bus is cleared and the peripheral re-initialised at most this often; in between transfers fail at once so
 * a dead bus cannot stall the cooperative loop for seconds. */
#define BSP_I2C2_RECOVERY_INTERVAL_MS 1000UL

static bool g_ready;
static bool g_bus_fault;
static uint32_t g_last_recovery_ms;
static uint32_t g_recoveries;

static bool status_is_set(uint32_t flag) {
    return I2C_GetStatus(BSP_I2C2, flag) == SET;
}

/* I2C-bus specification UM10204 rev. 7, 3.1.16 "Bus clear": an MCU reset in the middle of a read leaves the slave
 * driving a 0 on SDA, and no START is possible afterwards (HIL 2026-11-04: SDA held low after a J-Link reset, every
 * device returned START timeout). Clock SCL as open-drain GPIO up to nine times until SDA is released, then send a
 * STOP. Half periods of 5 us keep the 100 kHz bus timing. */
static bool bus_clear(void) {
    stc_gpio_init_t gpio;
    bool released;

    GPIO_SetFunc(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN, GPIO_FUNC_0);
    GPIO_SetFunc(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN, GPIO_FUNC_0);
    (void)GPIO_StructInit(&gpio);
    gpio.u16PinState = PIN_STAT_SET;
    gpio.u16PinDir = PIN_DIR_OUT;
    gpio.u16PinOutputType = PIN_OUT_TYPE_NMOS;
    (void)GPIO_Init(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN, &gpio);
    (void)GPIO_Init(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN, &gpio);
    DDL_DelayUS(5UL);
    for (unsigned i = 0U; i < 9U && GPIO_ReadInputPins(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN) == PIN_RESET; ++i) {
        GPIO_ResetPins(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN);
        DDL_DelayUS(5UL);
        GPIO_SetPins(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN);
        DDL_DelayUS(5UL);
    }
    released = GPIO_ReadInputPins(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN) == PIN_SET;
    /* STOP: SDA rises while SCL is high. */
    GPIO_ResetPins(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN);
    DDL_DelayUS(5UL);
    GPIO_ResetPins(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN);
    DDL_DelayUS(5UL);
    GPIO_SetPins(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN);
    DDL_DelayUS(5UL);
    GPIO_SetPins(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN);
    DDL_DelayUS(5UL);
    /* Back to the reset (input) configuration the I2C function was validated with. */
    (void)GPIO_StructInit(&gpio);
    (void)GPIO_Init(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN, &gpio);
    (void)GPIO_Init(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN, &gpio);
    return released;
}

static void finish_transfer(void) {
    I2C_AckConfig(BSP_I2C2, I2C_ACK);
    I2C_ClearStatus(BSP_I2C2, I2C_FLAG_CLR_ALL);
    I2C_Cmd(BSP_I2C2, DISABLE);
}

static void abort_transfer(void) {
    (void)I2C_Stop(BSP_I2C2, BSP_I2C2_TIMEOUT);
    finish_transfer();
}

static int address_error(int32_t result, bool receive) {
    if (status_is_set(I2C_FLAG_ARBITRATE_FAIL))
        return BSP_I2C2_ERR_ARBITRATION_LOST;
    if (result == LL_ERR || status_is_set(I2C_FLAG_NACKF))
        return receive ? BSP_I2C2_ERR_RX_ADDR_NACK : BSP_I2C2_ERR_ADDR_NACK;
    return receive ? BSP_I2C2_ERR_RX_ADDR_TIMEOUT : BSP_I2C2_ERR_ADDR_TIMEOUT;
}

static int data_error(void) {
    if (status_is_set(I2C_FLAG_ARBITRATE_FAIL))
        return BSP_I2C2_ERR_ARBITRATION_LOST;
    return status_is_set(I2C_FLAG_NACKF) ? BSP_I2C2_ERR_TX_NACK : BSP_I2C2_ERR_TX_TIMEOUT;
}

static int check_bus(void);

static int start_transfer(uint8_t address, uint8_t direction) {
    bool bus_was_busy;
    int32_t result;
    const int bus = check_bus();

    if (bus != BSP_I2C2_OK)
        return bus;
    I2C_Cmd(BSP_I2C2, ENABLE);
    I2C_SWResetCmd(BSP_I2C2, ENABLE);
    I2C_SWResetCmd(BSP_I2C2, DISABLE);
    bus_was_busy = status_is_set(I2C_FLAG_BUSY);
    result = I2C_Start(BSP_I2C2, BSP_I2C2_TIMEOUT);
    if (result != LL_OK) {
        int error = bus_was_busy ? BSP_I2C2_ERR_BUS_BUSY : BSP_I2C2_ERR_START_TIMEOUT;

        abort_transfer();
        /* No START means the bus is held (typically SDA by a slave): recover on the next transfer. */
        g_bus_fault = true;
        return error;
    }
    result = I2C_TransAddr(BSP_I2C2, address, direction, BSP_I2C2_TIMEOUT);
    if (result != LL_OK) {
        int error = address_error(result, direction == I2C_DIR_RX);

        abort_transfer();
        return error;
    }
    return BSP_I2C2_OK;
}

static int configure(void) {
    stc_i2c_init_t init;
    float32_t error;
    int32_t result;

    g_ready = false;
    g_bus_fault = !bus_clear();
    GPIO_SetFunc(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN, BSP_I2C2_SCL_FUNCTION);
    GPIO_SetFunc(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN, BSP_I2C2_SDA_FUNCTION);
    FCG_Fcg1PeriphClockCmd(BSP_I2C2_CLOCK, ENABLE);
    result = I2C_DeInit(BSP_I2C2);
    if (result != LL_OK)
        return BSP_I2C2_ERR_INIT_DEINIT;
    result = I2C_StructInit(&init);
    if (result != LL_OK)
        return BSP_I2C2_ERR_INIT_DEFAULTS;
    init.u32ClockDiv = I2C_CLK_DIV8;
    init.u32Baudrate = BSP_I2C2_BAUDRATE;
    init.u32SclTime = 3UL;
    result = I2C_Init(BSP_I2C2, &init, &error);
    if (result != LL_OK)
        return BSP_I2C2_ERR_INIT;
    I2C_BusWaitCmd(BSP_I2C2, ENABLE);
    g_ready = true;
    return BSP_I2C2_OK;
}

int bsp_i2c2_init(void) {
    g_last_recovery_ms = bsp_millis();
    return configure();
}

uint32_t bsp_i2c2_recovery_count(void) {
    return g_recoveries;
}

/* Called before every transfer: a bus marked faulted is cleared and re-initialised once the interval has passed. */
static int check_bus(void) {
    if (!g_bus_fault)
        return BSP_I2C2_OK;
    if ((uint32_t)(bsp_millis() - g_last_recovery_ms) < BSP_I2C2_RECOVERY_INTERVAL_MS)
        return BSP_I2C2_ERR_BUS_BUSY;
    g_last_recovery_ms = bsp_millis();
    ++g_recoveries;
    /* The App re-locks GPIO (PWPR) before its loop, which would silently drop the pin reconfiguration. Unlock only
     * for the recovery and restore the caller's state, so an init-time recovery does not lock GPIO early. FCG1
     * (the I2C2 clock gate) is not write-protected. */
    const bool gpio_locked = READ_REG16_BIT(CM_GPIO->PWPR, GPIO_PWPR_WE) == 0U;
    if (gpio_locked)
        GPIO_REG_Unlock();
    const int result = configure();
    if (gpio_locked)
        GPIO_REG_Lock();
    if (result != BSP_I2C2_OK)
        return result;
    return g_bus_fault ? BSP_I2C2_ERR_BUS_BUSY : BSP_I2C2_OK;
}

int bsp_i2c2_probe(uint8_t address) {
    int result;

    if (!g_ready)
        return BSP_I2C2_ERR_NOT_READY;
    if (address > 0x7FU)
        return BSP_I2C2_ERR_ARGUMENT;
    result = start_transfer(address, I2C_DIR_TX);
    if (result != BSP_I2C2_OK)
        return result;
    if (I2C_Stop(BSP_I2C2, BSP_I2C2_TIMEOUT) != LL_OK) {
        abort_transfer();
        return BSP_I2C2_ERR_STOP_TIMEOUT;
    }
    finish_transfer();
    return BSP_I2C2_OK;
}

int bsp_i2c2_write(uint8_t address, const uint8_t* data, uint32_t length) {
    int result;

    if (!g_ready)
        return BSP_I2C2_ERR_NOT_READY;
    if (address > 0x7FU || data == NULL || length == 0U)
        return BSP_I2C2_ERR_ARGUMENT;
    result = start_transfer(address, I2C_DIR_TX);
    if (result != BSP_I2C2_OK)
        return result;
    result = I2C_TransData(BSP_I2C2, data, length, BSP_I2C2_TIMEOUT);
    if (result != LL_OK || status_is_set(I2C_FLAG_NACKF)) {
        int error = data_error();

        abort_transfer();
        return error;
    }
    if (I2C_Stop(BSP_I2C2, BSP_I2C2_TIMEOUT) != LL_OK) {
        abort_transfer();
        return BSP_I2C2_ERR_STOP_TIMEOUT;
    }
    finish_transfer();
    return BSP_I2C2_OK;
}

int bsp_i2c2_read_reg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) {
    int result;

    if (!g_ready)
        return BSP_I2C2_ERR_NOT_READY;
    if (address > 0x7FU || data == NULL || length == 0U)
        return BSP_I2C2_ERR_ARGUMENT;
    result = start_transfer(address, I2C_DIR_TX);
    if (result != BSP_I2C2_OK)
        return result;
    result = I2C_TransData(BSP_I2C2, &reg, 1U, BSP_I2C2_TIMEOUT);
    if (result != LL_OK || status_is_set(I2C_FLAG_NACKF)) {
        int error = data_error();

        abort_transfer();
        return error;
    }
    if (I2C_Restart(BSP_I2C2, BSP_I2C2_TIMEOUT) != LL_OK) {
        int error =
            status_is_set(I2C_FLAG_ARBITRATE_FAIL) ? BSP_I2C2_ERR_ARBITRATION_LOST : BSP_I2C2_ERR_RESTART_TIMEOUT;

        abort_transfer();
        return error;
    }
    if (length == 1U)
        I2C_AckConfig(BSP_I2C2, I2C_NACK);
    result = I2C_TransAddr(BSP_I2C2, address, I2C_DIR_RX, BSP_I2C2_TIMEOUT);
    if (result != LL_OK) {
        int error = address_error(result, true);

        abort_transfer();
        return error;
    }
    result = I2C_MasterReceiveDataAndStop(BSP_I2C2, data, length, BSP_I2C2_TIMEOUT);
    I2C_AckConfig(BSP_I2C2, I2C_ACK);
    if (result != LL_OK) {
        int error =
            status_is_set(I2C_FLAG_ARBITRATE_FAIL) ? BSP_I2C2_ERR_ARBITRATION_LOST : BSP_I2C2_ERR_RX_STOP_TIMEOUT;

        abort_transfer();
        return error;
    }
    finish_transfer();
    return BSP_I2C2_OK;
}

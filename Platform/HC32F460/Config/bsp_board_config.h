#ifndef BSP_BOARD_CONFIG_H
#define BSP_BOARD_CONFIG_H

#include <stdint.h>

#define BOOT_EXTERNAL_WATCHDOG_IDLE_LEVEL   0U
#define BOOT_EXTERNAL_WATCHDOG_ACTIVE_LEVEL 1U
#define BOOT_EXTERNAL_WATCHDOG_INTERVAL_MS  3000U
#define BOOT_EXTERNAL_WATCHDOG_PULSE_MS     1U
#define BOOT_EXTERNAL_WATCHDOG_DEADLINE_MS  60000U
_Static_assert(BOOT_EXTERNAL_WATCHDOG_PULSE_MS > 0U, "watchdog pulse must be nonzero");
_Static_assert(BOOT_EXTERNAL_WATCHDOG_PULSE_MS < BOOT_EXTERNAL_WATCHDOG_INTERVAL_MS,
               "watchdog pulse must be shorter than its interval");
_Static_assert(BOOT_EXTERNAL_WATCHDOG_INTERVAL_MS < BOOT_EXTERNAL_WATCHDOG_DEADLINE_MS,
               "watchdog feed must precede the TPL5010 deadline");
_Static_assert(BOOT_EXTERNAL_WATCHDOG_IDLE_LEVEL != BOOT_EXTERNAL_WATCHDOG_ACTIVE_LEVEL, "watchdog levels must differ");
#if !defined(BOOT_HOST_TEST)
#define CONFIG_PERIPH_WE (LL_PERIPH_GPIO | LL_PERIPH_EFM | LL_PERIPH_FCG | LL_PERIPH_PWC_CLK_RMU | LL_PERIPH_SRAM)
#define CONFIG_PERIPH_WP CONFIG_PERIPH_WE
#define BSP_XTAL_PORT    GPIO_PORT_H
#define BSP_XTAL_PINS    (GPIO_PIN_00 | GPIO_PIN_01)
#define BOOT_EXTERNAL_WATCHDOG_PORT GPIO_PORT_A
#define BOOT_EXTERNAL_WATCHDOG_PIN  GPIO_PIN_06
/* SLAMRTK Charge Board V1.1: common-anode red/blue LEDs, active low, no green channel. */
#define BSP_STATUS_LED_PORT         GPIO_PORT_B
#define BSP_STATUS_LED_RED_PIN      GPIO_PIN_06
#define BSP_STATUS_LED_BLUE_PIN     GPIO_PIN_05
#define BSP_STATUS_LED_PINS         (BSP_STATUS_LED_RED_PIN | BSP_STATUS_LED_BLUE_PIN)
#define BSP_LOG_UART                CM_USART3
#define BSP_LOG_UART_CLOCK          FCG1_PERIPH_USART3
#define BSP_LOG_UART_TX_PORT        GPIO_PORT_B
#define BSP_LOG_UART_TX_PIN         GPIO_PIN_13
#define BSP_LOG_UART_TX_FUNCTION    GPIO_FUNC_32
#define BSP_LOG_UART_BAUDRATE       115200UL
#define BSP_USB_DATA_PORT           GPIO_PORT_A
#define BSP_USB_DATA_PINS           (GPIO_PIN_11 | GPIO_PIN_12)
/* V1.1 ties PA9 to VCC_3V3 through R320 0 Ohm: keep it an undriven input and use the software VBUS override. */
#define BSP_USB_VBUS_SENSE_ENABLE   0U
#define BSP_USB_VBUS_PORT           GPIO_PORT_A
#define BSP_USB_VBUS_PIN            GPIO_PIN_09
#define BSP_USB_VBUS_FUNCTION       GPIO_FUNC_10
#define BSP_I2C2                    CM_I2C2
#define BSP_I2C2_CLOCK              FCG1_PERIPH_I2C2
#define BSP_I2C2_SCL_PORT           GPIO_PORT_A
#define BSP_I2C2_SCL_PIN            GPIO_PIN_03
#define BSP_I2C2_SCL_FUNCTION       GPIO_FUNC_51
#define BSP_I2C2_SDA_PORT           GPIO_PORT_A
#define BSP_I2C2_SDA_PIN            GPIO_PIN_02
#define BSP_I2C2_SDA_FUNCTION       GPIO_FUNC_50
#define BSP_I2C2_BAUDRATE           100000UL
/* MP2762A INT# (CHARGE_STAT1) and inverted ACOK# (PG); both have external 100 kOhm pull-ups. */
#define BSP_CHARGER_SIGNAL_PORT     GPIO_PORT_A
#define BSP_CHARGER_INT_PIN         GPIO_PIN_04
#define BSP_CHARGER_PG_PIN          GPIO_PIN_05
#endif

#endif

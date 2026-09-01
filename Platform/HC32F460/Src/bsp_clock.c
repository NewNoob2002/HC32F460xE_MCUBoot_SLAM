#include "bsp_clock.h"
#include <stdint.h>
#include "bsp_board_config.h"
#include "bsp_write_protection.h"
#include "hc32_ll.h"

#define BSP_SYSTEM_CLOCK_HZ UINT32_C(200000000)

bool bsp_clock_init(void) {
    stc_clock_xtal_init_t stcXtalInit;
    stc_clock_pll_init_t stcMpllInit;
    bool ready = false;

    bsp_write_protection_unlock();
    GPIO_AnalogCmd(BSP_XTAL_PORT, BSP_XTAL_PINS, ENABLE);
    if (CLK_XtalStructInit(&stcXtalInit) != LL_OK || CLK_PLLStructInit(&stcMpllInit) != LL_OK)
        goto out;

    /* Set bus clk div. */
    CLK_SetClockDiv(CLK_BUS_CLK_ALL, (CLK_HCLK_DIV1 | CLK_EXCLK_DIV2 | CLK_PCLK0_DIV1 | CLK_PCLK1_DIV2 | CLK_PCLK2_DIV4
                                      | CLK_PCLK3_DIV4 | CLK_PCLK4_DIV2));

    /* Config Xtal and enable Xtal */
    stcXtalInit.u8Mode = CLK_XTAL_MD_OSC;
    stcXtalInit.u8Drv = CLK_XTAL_DRV_ULOW;
    stcXtalInit.u8State = CLK_XTAL_ON;
    stcXtalInit.u8StableTime = CLK_XTAL_STB_2MS;
    if (CLK_XtalInit(&stcXtalInit) != LL_OK)
        goto out;

    /* MPLL config (XTAL / pllmDiv * plln / PllpDiv = 200M). */
    stcMpllInit.PLLCFGR = 0UL;
    stcMpllInit.PLLCFGR_f.PLLM = 1UL - 1UL;
    stcMpllInit.PLLCFGR_f.PLLN = 50UL - 1UL;
    stcMpllInit.PLLCFGR_f.PLLP = 2UL - 1UL;
    stcMpllInit.PLLCFGR_f.PLLQ = 2UL - 1UL;
    stcMpllInit.PLLCFGR_f.PLLR = 2UL - 1UL;
    stcMpllInit.u8PLLState = CLK_PLL_ON;
    stcMpllInit.PLLCFGR_f.PLLSRC = CLK_PLL_SRC_XTAL;
    if (CLK_PLLInit(&stcMpllInit) != LL_OK || CLK_GetStableStatus(CLK_STB_FLAG_PLL) != SET)
        goto out;

    /* sram init include read/write wait cycle setting */
    SRAM_SetWaitCycle(SRAM_SRAMH, SRAM_WAIT_CYCLE0, SRAM_WAIT_CYCLE0);
    SRAM_SetWaitCycle((SRAM_SRAM12 | SRAM_SRAM3 | SRAM_SRAMR), SRAM_WAIT_CYCLE1, SRAM_WAIT_CYCLE1);

    /* flash read wait cycle setting */
    if (EFM_SetWaitCycle(EFM_WAIT_CYCLE5) != LL_OK)
        goto out;
    /* 3 cycles for 126MHz ~ 200MHz */
    GPIO_SetReadWaitCycle(GPIO_RD_WAIT3);
    /* Switch driver ability */
    if (PWC_HighSpeedToHighPerformance() != LL_OK)
        goto out;
    /* Switch system clock source to MPLL. */
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);
    /* Reset cache ram */
    EFM_CacheRamReset(ENABLE);
    EFM_CacheRamReset(DISABLE);
    /* Enable cache */
    EFM_CacheCmd(ENABLE);

    SystemCoreClockUpdate();
    ready = SystemCoreClock == BSP_SYSTEM_CLOCK_HZ;

out:
    bsp_write_protection_restore();
    return ready;
}

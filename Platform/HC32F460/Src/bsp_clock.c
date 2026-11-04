#include "bsp_clock.h"
#include <stdint.h>
#include "bsp_board_config.h"
#include "hc32_ll_clk.h"
#include "hc32_ll_efm.h"
#include "hc32_ll_gpio.h"
#include "hc32_ll_pwc.h"
#include "hc32_ll_sram.h"

#define BSP_SYSTEM_CLOCK_HZ UINT32_C(200000000)
#define BSP_PLL_VCO_HZ      UINT32_C(400000000)

bool bsp_clock_init(void) {
    stc_clock_xtal_init_t stcXtalInit;
    stc_clock_pll_init_t stcMpllInit;
    bool ready = false;

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
    return ready;
}

bool bsp_clock_set_pll_hz(uint32_t hz) {
    /* SYSCLK = 400 MHz MPLL VCO / P. Rates above 50 MHz would need non-unity bus dividers; below 25 MHz is untested
     * for USB (HIL: USB does not enumerate with the 8 MHz XTAL as SYSCLK). */
    const uint32_t pll_p = hz != 0U ? BSP_PLL_VCO_HZ / hz : 0U;
    if (hz < UINT32_C(25000000) || hz > UINT32_C(50000000) || pll_p * hz != BSP_PLL_VCO_HZ)
        return false;
    if (CLK_GetStableStatus(CLK_STB_FLAG_XTAL) != SET)
        return false;
    /* Run from the XTAL while MPLL is reprogrammed. Dropping the source first means the old dividers only ever slow
     * the buses; every bus then runs at SYSCLK, which all buses allow up to 50 MHz. */
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_XTAL);
    CLK_SetClockDiv(CLK_BUS_CLK_ALL, (CLK_HCLK_DIV1 | CLK_EXCLK_DIV1 | CLK_PCLK0_DIV1 | CLK_PCLK1_DIV1 | CLK_PCLK2_DIV1
                                      | CLK_PCLK3_DIV1 | CLK_PCLK4_DIV1));
    if (CLK_PLLCmd(DISABLE) != LL_OK)
        return false;
    /* Conservative wait states for <=50 MHz (extra cycles only cost speed); set while the clock is at 8 MHz. */
    if (EFM_SetWaitCycle(hz > UINT32_C(33000000) ? EFM_WAIT_CYCLE2 : EFM_WAIT_CYCLE1) != LL_OK)
        return false;
    GPIO_SetReadWaitCycle(hz > UINT32_C(42000000) ? GPIO_RD_WAIT1 : GPIO_RD_WAIT0);
    stc_clock_pll_init_t pll;
    if (CLK_PLLStructInit(&pll) != LL_OK)
        return false;
    pll.PLLCFGR = 0UL;
    pll.PLLCFGR_f.PLLM = 1UL - 1UL;
    pll.PLLCFGR_f.PLLN = 50UL - 1UL;
    pll.PLLCFGR_f.PLLP = pll_p - 1UL;
    pll.PLLCFGR_f.PLLQ = 2UL - 1UL;
    pll.PLLCFGR_f.PLLR = 2UL - 1UL;
    pll.u8PLLState = CLK_PLL_ON;
    pll.PLLCFGR_f.PLLSRC = CLK_PLL_SRC_XTAL;
    if (CLK_PLLInit(&pll) != LL_OK || CLK_GetStableStatus(CLK_STB_FLAG_PLL) != SET)
        return false;
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_PLL);
    /* High-speed (<=168 MHz) drive mode needs less core voltage than the high-performance mode used at 200 MHz. */
    if (PWC_HighPerformanceToHighSpeed() != LL_OK)
        return false;
    SystemCoreClockUpdate();
    return SystemCoreClock == hz;
}

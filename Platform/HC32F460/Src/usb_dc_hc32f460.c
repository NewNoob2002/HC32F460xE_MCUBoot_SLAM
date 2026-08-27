#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hc32_ll.h"
#include "hc32_ll_interrupts.h"
#include "hc32_ll_usb.h"
#include "bsp_write_protection.h"
#include "usbd_core.h"

#define HC32_USB_BUS_ID 0U
#define HC32_USB_IRQ INT003_IRQn

#define XFER_COMPL  (1UL << 0)
#define EP_DISABLED (1UL << 1)
#define SETUP_BIT   (1UL << 3)
#define TIME_OUT    (1UL << 3)
#define TX_FIFO_EMP (1UL << 7)

#define MODE_MISMATCH_INT (1UL << 1)
#define RX_FIFO_INT       (1UL << 4)
#define USB_SUSPEND_INT   (1UL << 11)
#define USB_RESET_INT     (1UL << 12)
#define ENUM_DONE_INT     (1UL << 13)
#define IN_EP_INT         (1UL << 18)
#define OUT_EP_INT        (1UL << 19)
#define WAKEUP_INT        (1UL << 31)

#define RX_STATUS_DATA  2U
#define RX_STATUS_SETUP 6U

struct hc32_usb_device {
    USB_CORE_BASIC_CFGS basic;
    LL_USB_TypeDef regs;
    USB_DEV_EP in_ep[USB_MAX_EP_NUM];
    USB_DEV_EP out_ep[USB_MAX_EP_NUM];
    uint8_t setup[8];
};

static struct hc32_usb_device g_usb;
volatile uint32_t g_hc32_usb_init_stage;

void usb_udelay(uint32_t usec)
{
    DDL_DelayUS(usec);
}

void usb_mdelay(uint32_t msec)
{
    DDL_DelayMS(msec);
}

static int usb_low_level_init(void)
{
    stc_clock_pllx_init_t pll;
    stc_gpio_init_t gpio;

    g_hc32_usb_init_stage = 0x100U;
    bsp_write_protection_unlock();
    (void)CLK_PLLxStructInit(&pll);
    pll.u8PLLState = CLK_PLLX_ON;
    pll.PLLCFGR = 0UL;
    pll.PLLCFGR_f.PLLM = 2UL - 1UL;
    pll.PLLCFGR_f.PLLN = 120UL - 1UL;
    pll.PLLCFGR_f.PLLP = 10UL - 1UL;
    pll.PLLCFGR_f.PLLQ = 6UL - 1UL;
    pll.PLLCFGR_f.PLLR = 6UL - 1UL;
    int32_t result = CLK_PLLxInit(&pll);
    if (result != LL_OK) {
        g_hc32_usb_init_stage = 0xE1000000UL | ((uint32_t)result & 0xFFFFUL);
        bsp_write_protection_restore();
        return -1;
    }
    CLK_SetUSBClockSrc(CLK_USBCLK_PLLXP);

    (void)GPIO_StructInit(&gpio);
    gpio.u16PinAttr = PIN_ATTR_ANALOG;
    (void)GPIO_Init(GPIO_PORT_A, GPIO_PIN_11 | GPIO_PIN_12, &gpio);
    GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_09, GPIO_FUNC_10);

    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USBFS, ENABLE);
    bsp_write_protection_restore();
    g_hc32_usb_init_stage = 0x200U;
    return 0;
}

static USB_DEV_EP *usb_ep(uint8_t address)
{
    uint8_t index = USB_EP_GET_IDX(address);

    if (index >= USB_MAX_EP_NUM) {
        return NULL;
    }
    return USB_EP_DIR_IS_IN(address) ? &g_usb.in_ep[index] : &g_usb.out_ep[index];
}

static void usb_open_ep(uint8_t address, uint16_t mps, uint8_t type)
{
    USB_DEV_EP *ep = usb_ep(address);

    ep->epidx = USB_EP_GET_IDX(address);
    ep->ep_dir = USB_EP_DIR_IS_IN(address) ? 1U : 0U;
    ep->maxpacket = mps;
    ep->trans_type = type;
    ep->ep_stall = 0U;
    ep->tx_fifo_num = ep->epidx;
    usb_epactive(&g_usb.regs, ep);
}

static void usb_start_write(uint8_t address, const uint8_t *data, uint32_t length)
{
    USB_DEV_EP *ep = usb_ep(address);

    ep->ep_dir = 1U;
    ep->epidx = USB_EP_GET_IDX(address);
    ep->xfer_buff = (uint8_t *)data;
    ep->xfer_count = 0UL;
    ep->xfer_len = length;
    if (ep->epidx == 0U) {
        usb_ep0transbegin(&g_usb.regs, ep, 0U);
    } else {
        usb_epntransbegin(&g_usb.regs, ep, 0U);
    }
}

static void usb_start_read(uint8_t address, uint8_t *data, uint32_t length)
{
    USB_DEV_EP *ep = usb_ep(address);

    ep->ep_dir = 0U;
    ep->epidx = USB_EP_GET_IDX(address);
    ep->xfer_buff = data;
    ep->xfer_count = 0UL;
    ep->xfer_len = length;
    ep->rem_data_len = length;
    if (ep->epidx == 0U) {
        usb_ep0transbegin(&g_usb.regs, ep, 0U);
    } else {
        usb_epntransbegin(&g_usb.regs, ep, 0U);
    }
}

static uint32_t usb_in_ep_interrupt(uint8_t epnum)
{
    uint32_t mask = READ_REG32(g_usb.regs.DREGS->DIEPMSK);

    mask |= ((READ_REG32(g_usb.regs.DREGS->DIEPEMPMSK) >> epnum) & 1UL) << 7U;
    return READ_REG32(g_usb.regs.INEP_REGS[epnum]->DIEPINT) & mask;
}

static void usb_fill_tx_fifo(uint8_t epnum)
{
    USB_DEV_EP *ep = &g_usb.in_ep[epnum];
    uint32_t length = ep->xfer_len - ep->xfer_count;

    if (length > ep->maxpacket) {
        length = ep->maxpacket;
    }
    while (((READ_REG32(g_usb.regs.INEP_REGS[epnum]->DTXFSTS) & USBFS_DTXFSTS_INEPTFSAV) >=
            ((length + 3UL) >> 2U)) &&
           (ep->xfer_count < ep->xfer_len)) {
        usb_wrpkt(&g_usb.regs, ep->xfer_buff, epnum, (uint16_t)length, 0U);
        ep->xfer_buff += length;
        ep->xfer_count += length;
        length = ep->xfer_len - ep->xfer_count;
        if (length > ep->maxpacket) {
            length = ep->maxpacket;
        }
    }
    if (length == 0UL) {
        CLR_REG32_BIT(g_usb.regs.DREGS->DIEPEMPMSK, 1UL << epnum);
    }
}

static void usb_handle_rx_fifo(void)
{
    uint32_t status;
    uint8_t epnum;
    uint8_t packet_status;
    uint16_t count;
    USB_DEV_EP *ep;

    CLR_REG32_BIT(g_usb.regs.GREGS->GINTMSK, USBFS_GINTMSK_RXFNEM);
    status = READ_REG32(g_usb.regs.GREGS->GRXSTSP);
    epnum = (uint8_t)(status & USBFS_GRXSTSP_CHNUM_EPNUM);
    packet_status = (uint8_t)((status & USBFS_GRXSTSP_PKTSTS) >> USBFS_GRXSTSP_PKTSTS_POS);
    count = (uint16_t)((status & USBFS_GRXSTSP_BCNT) >> USBFS_GRXSTSP_BCNT_POS);

    if (epnum < USB_MAX_EP_NUM) {
        ep = &g_usb.out_ep[epnum];
        if ((packet_status == RX_STATUS_DATA) && (count != 0U) && (ep->xfer_buff != NULL)) {
            usb_rdpkt(&g_usb.regs, ep->xfer_buff, count);
            ep->xfer_buff += count;
            ep->xfer_count += count;
        } else if (packet_status == RX_STATUS_SETUP) {
            usb_rdpkt(&g_usb.regs, g_usb.setup, sizeof(g_usb.setup));
        }
    }
    SET_REG32_BIT(g_usb.regs.GREGS->GINTMSK, USBFS_GINTMSK_RXFNEM);
}

static void usb_handle_in_ep(void)
{
    uint32_t pending = usb_getalliepintr(&g_usb.regs);

    for (uint8_t epnum = 0U; (pending != 0UL) && (epnum < USB_MAX_EP_NUM); ++epnum, pending >>= 1U) {
        if ((pending & 1UL) == 0UL) {
            continue;
        }
        uint32_t interrupt = usb_in_ep_interrupt(epnum);
        if ((interrupt & XFER_COMPL) != 0UL) {
            CLR_REG32_BIT(g_usb.regs.DREGS->DIEPEMPMSK, 1UL << epnum);
            WRITE_REG32(g_usb.regs.INEP_REGS[epnum]->DIEPINT, XFER_COMPL);
            usbd_event_ep_in_complete_handler(HC32_USB_BUS_ID, epnum | USB_EP_DIR_IN,
                                              g_usb.in_ep[epnum].xfer_count);
        }
        if ((interrupt & EP_DISABLED) != 0UL) {
            WRITE_REG32(g_usb.regs.INEP_REGS[epnum]->DIEPINT, EP_DISABLED);
        }
        if ((interrupt & TIME_OUT) != 0UL) {
            WRITE_REG32(g_usb.regs.INEP_REGS[epnum]->DIEPINT, TIME_OUT);
        }
        if ((interrupt & TX_FIFO_EMP) != 0UL) {
            usb_fill_tx_fifo(epnum);
            WRITE_REG32(g_usb.regs.INEP_REGS[epnum]->DIEPINT, TX_FIFO_EMP);
        }
    }
}

static void usb_handle_out_ep(void)
{
    uint32_t pending = usb_getalloepintr(&g_usb.regs);

    for (uint8_t epnum = 0U; (pending != 0UL) && (epnum < USB_MAX_EP_NUM); ++epnum, pending >>= 1U) {
        if ((pending & 1UL) == 0UL) {
            continue;
        }
        uint32_t interrupt = usb_getoepintbit(&g_usb.regs, epnum);
        if ((interrupt & XFER_COMPL) != 0UL) {
            WRITE_REG32(g_usb.regs.OUTEP_REGS[epnum]->DOEPINT, XFER_COMPL);
            usbd_event_ep_out_complete_handler(HC32_USB_BUS_ID, epnum,
                                               g_usb.out_ep[epnum].xfer_count);
        }
        if ((interrupt & EP_DISABLED) != 0UL) {
            WRITE_REG32(g_usb.regs.OUTEP_REGS[epnum]->DOEPINT, EP_DISABLED);
        }
        if ((epnum == 0U) && ((usb_getoepintbit(&g_usb.regs, epnum) & SETUP_BIT) != 0UL)) {
            usbd_event_ep0_setup_complete_handler(HC32_USB_BUS_ID, g_usb.setup);
            WRITE_REG32(g_usb.regs.OUTEP_REGS[epnum]->DOEPINT, SETUP_BIT);
        }
    }
}

static void usb_handle_reset(void)
{
    CLR_REG32_BIT(g_usb.regs.DREGS->DCTL, USBFS_DCTL_RWUSIG);
    usb_txfifoflush(&g_usb.regs, 0UL);
    for (uint8_t i = 0U; i < g_usb.basic.dev_epnum; ++i) {
        WRITE_REG32(g_usb.regs.INEP_REGS[i]->DIEPINT, 0xFFUL);
        WRITE_REG32(g_usb.regs.OUTEP_REGS[i]->DOEPINT, 0xFFUL);
    }
    WRITE_REG32(g_usb.regs.DREGS->DAINT, 0xFFFFFFFFUL);
    WRITE_REG32(g_usb.regs.DREGS->DAINTMSK, 1UL | (1UL << USBFS_DAINTMSK_OEPINTM_POS));
    WRITE_REG32(g_usb.regs.DREGS->DOEPMSK,
                USBFS_DOEPMSK_STUPM | USBFS_DOEPMSK_XFRCM | USBFS_DOEPMSK_EPDM);
    WRITE_REG32(g_usb.regs.DREGS->DIEPMSK,
                USBFS_DIEPMSK_XFRCM | USBFS_DIEPMSK_TOM | USBFS_DIEPMSK_EPDM);
    usb_devaddrset(&g_usb.regs, 0U);
    usb_ep0revcfg(&g_usb.regs, 0U, g_usb.setup);
    WRITE_REG32(g_usb.regs.GREGS->GINTSTS, USBFS_GINTSTS_USBRST);
    usbd_event_reset_handler(HC32_USB_BUS_ID);
}

static void usb_irq_handler(void)
{
    uint32_t interrupt;

    if (usb_getcurmod(&g_usb.regs) != DEVICE_MODE) {
        return;
    }
    interrupt = usb_getcoreintr(&g_usb.regs);
    if ((interrupt & OUT_EP_INT) != 0UL) {
        usb_handle_out_ep();
    }
    if ((interrupt & IN_EP_INT) != 0UL) {
        usb_handle_in_ep();
    }
    if ((interrupt & RX_FIFO_INT) != 0UL) {
        usb_handle_rx_fifo();
    }
    if ((interrupt & USB_RESET_INT) != 0UL) {
        usb_handle_reset();
    }
    if ((interrupt & ENUM_DONE_INT) != 0UL) {
        usb_ep0activate(&g_usb.regs);
        SET_REG32_BIT(g_usb.regs.GREGS->GUSBCFG, USBFS_GUSBCFG_TRDT);
        WRITE_REG32(g_usb.regs.GREGS->GINTSTS, USBFS_GINTSTS_ENUMDNE);
    }
    if ((interrupt & USB_SUSPEND_INT) != 0UL) {
        WRITE_REG32(g_usb.regs.GREGS->GINTSTS, USBFS_GINTSTS_USBSUSP);
        usbd_event_suspend_handler(HC32_USB_BUS_ID);
    }
    if ((interrupt & WAKEUP_INT) != 0UL) {
        WRITE_REG32(g_usb.regs.GREGS->GINTSTS, USBFS_GINTSTS_WKUINT);
        usbd_event_resume_handler(HC32_USB_BUS_ID);
    }
    if ((interrupt & MODE_MISMATCH_INT) != 0UL) {
        WRITE_REG32(g_usb.regs.GREGS->GINTSTS, MODE_MISMATCH_INT);
    }
}

int usb_dc_init(uint8_t busid)
{
    stc_usb_port_identify port = { .u8CoreID = USBFS_CORE_ID };
    stc_irq_signin_config_t irq = {
        .enIRQn = HC32_USB_IRQ,
        .enIntSrc = INT_SRC_USBFS_GLB,
        .pfnCallback = usb_irq_handler,
    };

    if ((busid != HC32_USB_BUS_ID) || (usb_low_level_init() != 0)) {
        return -1;
    }
    memset(&g_usb, 0, sizeof(g_usb));
    usb_setregaddr(&g_usb.regs, &port, &g_usb.basic);
    usb_gintdis(&g_usb.regs);
    usb_initusbcore(&g_usb.regs, &g_usb.basic);
    usb_modeset(&g_usb.regs, DEVICE_MODE);
    usb_devmodeinit(&g_usb.regs, &g_usb.basic);
    g_hc32_usb_init_stage = 0x300U;
    CLR_REG32_BIT(g_usb.regs.GREGS->GINTMSK, USBFS_GINTMSK_SOFM);

    if (INTC_IrqSignIn(&irq) != LL_OK) {
        g_hc32_usb_init_stage = 0xE3000000UL;
        usb_ctrldevconnect(&g_usb.regs, 1U);
        return -1;
    }
    NVIC_ClearPendingIRQ(HC32_USB_IRQ);
    NVIC_SetPriority(HC32_USB_IRQ, DDL_IRQ_PRIO_DEFAULT);
    NVIC_EnableIRQ(HC32_USB_IRQ);
    usb_ginten(&g_usb.regs);
    g_hc32_usb_init_stage = 0x400U;
    return 0;
}

int usb_dc_deinit(uint8_t busid)
{
    if (busid != HC32_USB_BUS_ID) {
        return -1;
    }
    usb_ctrldevconnect(&g_usb.regs, 1U);
    usb_gintdis(&g_usb.regs);
    NVIC_DisableIRQ(HC32_USB_IRQ);
    (void)INTC_IrqSignOut(HC32_USB_IRQ);
    FCG_Fcg1PeriphClockCmd(FCG1_PERIPH_USBFS, DISABLE);
    return 0;
}

int usbd_set_address(uint8_t busid, const uint8_t address)
{
    if (busid != HC32_USB_BUS_ID) {
        return -1;
    }
    usb_devaddrset(&g_usb.regs, address);
    return 0;
}

int usbd_set_remote_wakeup(uint8_t busid)
{
    if ((busid != HC32_USB_BUS_ID) ||
        ((READ_REG32(g_usb.regs.DREGS->DSTS) & USBFS_DSTS_SUSPSTS) == 0UL)) {
        return -1;
    }
    usb_remotewakeupen(&g_usb.regs);
    return 0;
}

uint8_t usbd_get_port_speed(uint8_t busid)
{
    (void)busid;
    return USB_SPEED_FULL;
}

int usbd_ep_open(uint8_t busid, const struct usb_endpoint_descriptor *descriptor)
{
    uint8_t index;

    if ((busid != HC32_USB_BUS_ID) || (descriptor == NULL)) {
        return -1;
    }
    index = USB_EP_GET_IDX(descriptor->bEndpointAddress);
    if (index >= USB_MAX_EP_NUM) {
        return -1;
    }
    usb_open_ep(descriptor->bEndpointAddress,
                USB_GET_MAXPACKETSIZE(descriptor->wMaxPacketSize),
                USB_GET_ENDPOINT_TYPE(descriptor->bmAttributes));
    return 0;
}

int usbd_ep_close(uint8_t busid, const uint8_t address)
{
    USB_DEV_EP *ep = usb_ep(address);

    if ((busid != HC32_USB_BUS_ID) || (ep == NULL)) {
        return -1;
    }
    usb_epdeactive(&g_usb.regs, ep);
    return 0;
}

int usbd_ep_set_stall(uint8_t busid, const uint8_t address)
{
    USB_DEV_EP *ep = usb_ep(address);

    if ((busid != HC32_USB_BUS_ID) || (ep == NULL)) {
        return -1;
    }
    ep->ep_stall = 1U;
    usb_setepstall(&g_usb.regs, ep);
    return 0;
}

int usbd_ep_clear_stall(uint8_t busid, const uint8_t address)
{
    USB_DEV_EP *ep = usb_ep(address);

    if ((busid != HC32_USB_BUS_ID) || (ep == NULL)) {
        return -1;
    }
    ep->ep_stall = 0U;
    usb_clearepstall(&g_usb.regs, ep);
    return 0;
}

int usbd_ep_is_stalled(uint8_t busid, const uint8_t address, uint8_t *stalled)
{
    USB_DEV_EP *ep = usb_ep(address);

    if ((busid != HC32_USB_BUS_ID) || (ep == NULL) || (stalled == NULL)) {
        return -1;
    }
    *stalled = ep->ep_stall;
    return 0;
}

int usbd_ep_start_write(uint8_t busid, const uint8_t address,
                        const uint8_t *data, uint32_t length)
{
    USB_DEV_EP *ep = usb_ep(address);

    if ((busid != HC32_USB_BUS_ID) || !USB_EP_DIR_IS_IN(address) ||
        (ep == NULL) || ((ep->epidx != 0U) && (ep->maxpacket == 0UL)) ||
        ((data == NULL) && (length != 0UL)) ||
        (((uintptr_t)data & 3UL) != 0UL)) {
        return -1;
    }
    usb_start_write(address, data, length);
    return 0;
}

int usbd_ep_start_read(uint8_t busid, const uint8_t address,
                       uint8_t *data, uint32_t length)
{
    USB_DEV_EP *ep = usb_ep(address);

    if ((busid != HC32_USB_BUS_ID) || !USB_EP_DIR_IS_OUT(address) ||
        (ep == NULL) || ((ep->epidx != 0U) && (ep->maxpacket == 0UL)) ||
        ((data == NULL) && (length != 0UL)) ||
        (((uintptr_t)data & 3UL) != 0UL) ||
        ((ep->epidx != 0U) && (length != 0UL) && ((length % ep->maxpacket) != 0UL))) {
        return -1;
    }
    usb_start_read(address, data, length);
    return 0;
}

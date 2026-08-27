#ifndef USB_APP_CONF_H
#define USB_APP_CONF_H

#define USB_FS_MODE
#define USE_DEVICE_MODE

#define RX_FIFO_FS_SIZE  (128U)
#define TX0_FIFO_FS_SIZE (32U)
#define TX1_FIFO_FS_SIZE (32U)
#define TX2_FIFO_FS_SIZE (32U)
#define TX3_FIFO_FS_SIZE (32U)
#define TX4_FIFO_FS_SIZE (32U)
#define TX5_FIFO_FS_SIZE (32U)

#if ((RX_FIFO_FS_SIZE + TX0_FIFO_FS_SIZE + TX1_FIFO_FS_SIZE + \
      TX2_FIFO_FS_SIZE + TX3_FIFO_FS_SIZE + TX4_FIFO_FS_SIZE + \
      TX5_FIFO_FS_SIZE) > 320U)
#error "HC32F460 USB FIFO allocation exceeds 320 words"
#endif

#endif

#ifndef USB_CONFIG_H
#define USB_CONFIG_H

#define CONFIG_USB_PRINTF(...)           ((void)0)
#define CONFIG_USB_DBG_LEVEL             (-1)
#define CONFIG_USB_ALIGN_SIZE            4
/* App MS OS 2.0 composite descriptor set is 338 bytes; CherryUSB copies it through EP0. */
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#define CONFIG_USBDEV_MAX_BUS            1
#define USB_NOCACHE_RAM_SECTION

#endif

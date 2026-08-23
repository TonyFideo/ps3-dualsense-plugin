#ifndef DUALSENSE_USB_H
#define DUALSENSE_USB_H

#include "ds_types.h"

#define DUALSENSE_VENDOR_ID  0x054c
#define DUALSENSE_PRODUCT_ID 0x0ce6

s32 dualsense_usb_init(void);
void dualsense_usb_shutdown(void);

#endif

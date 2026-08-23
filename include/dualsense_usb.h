#ifndef DUALSENSE_USB_H
#define DUALSENSE_USB_H

#include <ppu-lv2.h>
#include <stdint.h>

// USB standard descriptors (from USB spec)
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed)) UsbDeviceDescriptor;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t MaxPower;
} __attribute__((packed)) UsbConfigurationDescriptor;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed)) UsbInterfaceDescriptor;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__((packed)) UsbEndpointDescriptor;

// Cell Usbd structs
typedef struct {
    const char *name;
    int32_t (*probe)(int32_t dev_id);
    int32_t (*attach)(int32_t dev_id);
    int32_t (*detach)(int32_t dev_id);
} CellUsbdLddOps;

// DualSense Constants
#define DUALSENSE_VID 0x054C
#define DUALSENSE_PID 0x0CE6

// Function prototypes
int32_t init_dualsense_usb(void);
int32_t shutdown_dualsense_usb(void);

#endif

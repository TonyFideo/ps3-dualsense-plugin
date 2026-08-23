#include "dualsense_usb.h"
#include <ppu-lv2.h>
#include <sysmodule/sysmodule.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// LV2 Syscall wrappers for Virtual Pad
#define system_call_4(nv, a1, a2, a3, a4) lv2syscall4(nv, (uint64_t)(a1), (uint64_t)(a2), (uint64_t)(a3), (uint64_t)(a4))

static inline void sys_pad_dbg_ldd_register_controller(uint8_t *data, int32_t *handle, uint8_t addr, uint32_t capability) {
    system_call_4(574, (uint8_t *)data, (int32_t *)handle, (uint8_t)addr, (uint32_t)capability);
}

static inline void sys_pad_dbg_ldd_set_data_insert_mode(int32_t handle, uint16_t addr, uint32_t *mode, uint8_t addr2) {
    system_call_4(573, handle, addr, mode, addr2);
}

// Global Variables
static int32_t virtual_pad_handle = -1;
static int32_t usbd_initialized = 0;

// Since we may not have libusbd statically linked, we declare the functions here
// In a full implementation, we would resolve these NIDs dynamically if we didn't have libusbd
extern int32_t cellUsbdInit(void);
extern int32_t cellUsbdRegisterExtraLdd(CellUsbdLddOps *lddOps, uint16_t vendor_id, uint16_t product_id);
extern int32_t cellUsbdUnregisterExtraLdd(CellUsbdLddOps *lddOps);
extern void *cellUsbdScanStaticDescriptor(int32_t dev_id, void *ptr, uint8_t type);
extern int32_t cellUsbdOpenPipe(int32_t dev_id, UsbEndpointDescriptor *desc);
extern int32_t cellUsbdInterruptTransfer(int32_t pipe_id, uint8_t *buf, int32_t size, void (*cb)(int32_t, int32_t, void *), void *arg);

// USB Descriptors definitions
#define USB_DESCRIPTOR_TYPE_DEVICE 0x01
#define USB_DESCRIPTOR_TYPE_CONFIGURATION 0x02
#define USB_DESCRIPTOR_TYPE_INTERFACE 0x04
#define USB_DESCRIPTOR_TYPE_ENDPOINT 0x05

// Callbacks for CellUsbdLddOps
static int32_t dualsense_probe(int32_t dev_id) {
    UsbDeviceDescriptor *ddesc;
    
    // Scan descriptor
    ddesc = (UsbDeviceDescriptor *)cellUsbdScanStaticDescriptor(dev_id, NULL, USB_DESCRIPTOR_TYPE_DEVICE);
    if (ddesc) {
        // Swap endianness since PS3 is Big Endian and USB is Little Endian
        uint16_t idVendor = (ddesc->idVendor << 8) | (ddesc->idVendor >> 8);
        uint16_t idProduct = (ddesc->idProduct << 8) | (ddesc->idProduct >> 8);
        
        if (idVendor == DUALSENSE_VID && idProduct == DUALSENSE_PID) {
            return 0; // CELL_USBD_PROBE_SUCCEEDED
        }
    }
    return -1; // CELL_USBD_PROBE_FAILED
}

static int32_t dualsense_attach(int32_t dev_id) {
    // Locate the endpoints and initialize the connection
    uint8_t data[0x114];
    memset(data, 0, sizeof(data));
    
    // Register Virtual Pad
    if (virtual_pad_handle < 0) {
        uint32_t capability = 0xFFFF; // All capabilities
        sys_pad_dbg_ldd_register_controller(data, &virtual_pad_handle, 5, capability << 1);
        
        // Wait a bit
        // sys_timer_usleep(1000 * 10);
        
        if (virtual_pad_handle >= 0) {
            uint32_t mode = 1; // CELL_PAD_LDD_INSERT_DATA_INTO_GAME_MODE_ON
            sys_pad_dbg_ldd_set_data_insert_mode(virtual_pad_handle, 0x100, &mode, 4);
        }
    }
    
    // TODO: Open pipes and start the polling thread for Input Report 0x01
    return 0; // CELL_USBD_ATTACH_SUCCEEDED
}

static int32_t dualsense_detach(int32_t dev_id) {
    // Unregister virtual pad
    if (virtual_pad_handle >= 0) {
        // Here we'd call cellPadLddUnregisterController, but since it's an LDD syscall we can just un-insert
        // Actually PS3xPAD uses cellPadLddUnregisterController(virtual_pad_handle);
        virtual_pad_handle = -1;
    }
    return 0; // CELL_USBD_DETACH_SUCCEEDED
}

static CellUsbdLddOps dualsense_ops = {
    "DualSense",
    dualsense_probe,
    dualsense_attach,
    dualsense_detach
};

int32_t init_dualsense_usb(void) {
    if (sysModuleLoad(SYSMODULE_USBD) == 0) {
        cellUsbdInit();
        cellUsbdRegisterExtraLdd(&dualsense_ops, DUALSENSE_VID, DUALSENSE_PID);
        usbd_initialized = 1;
        return 0;
    }
    return -1;
}

int32_t shutdown_dualsense_usb(void) {
    if (usbd_initialized) {
        cellUsbdUnregisterExtraLdd(&dualsense_ops);
        sysModuleUnload(SYSMODULE_USBD);
        usbd_initialized = 0;
    }
    return 0;
}

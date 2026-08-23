#include <io/pad.h>
#include <ppu-lv2.h>
#include <sys/systime.h>
#include <sysmodule/sysmodule.h>
#include <usb/usb.h>

#include "diagnostics.h"
#include "dualsense_usb.h"

#define DS_REPORT_SIZE 64
#define DS_REPORT_ID 0x01
#define DS_PAD_LENGTH 24
#define DS_INSERT_GAME_MODE 1u
#define DS_CAPABILITIES ((1u << 0) | (1u << 3))
#define SWAP16(v) ((u16)((((u16)(v) & 0xffu) << 8) | (((u16)(v) >> 8) & 0xffu)))

typedef struct ds_context {
    s32 device_id;
    s32 control_pipe;
    s32 input_pipe;
    s32 output_pipe;
    s32 pad_handle;
    u8 configuration;
    u8 interface_number;
    u8 alternate_setting;
    volatile u8 attached;
    u8 transfer_error_reported;
    u8 insert_error_reported;
    u8 *report;
} ds_context;

static ds_context g_ds;
static s32 g_module_loaded;
static s32 g_usb_initialized;
static s32 g_driver_registered;

static s32 ds_probe(s32 device_id);
static s32 ds_attach(s32 device_id);
static s32 ds_detach(s32 device_id);
static void configuration_done(s32 result, s32 count, void *arg);
static void interface_done(s32 result, s32 count, void *arg);
static void input_done(s32 result, s32 count, void *arg);

static usbLddOps g_driver = {
    "DualSense Fix", ds_probe, ds_attach, ds_detach
};

static void memory_zero(void *pointer, u32 size)
{
    u8 *bytes = (u8 *)pointer;
    while (size-- != 0u) {
        *bytes++ = 0;
    }
}

static void context_reset(void)
{
    memory_zero(&g_ds, sizeof(g_ds));
    g_ds.device_id = -1;
    g_ds.control_pipe = -1;
    g_ds.input_pipe = -1;
    g_ds.output_pipe = -1;
    g_ds.pad_handle = -1;
}

static void debug_register_pad(u8 *scratch, s32 *handle)
{
    lv2syscall4(574, (u64)scratch, (u64)handle, 5u,
                (u64)(DS_CAPABILITIES << 1));
}

static void debug_enable_game_insertion(s32 handle)
{
    u32 mode = DS_INSERT_GAME_MODE;
    lv2syscall4(573, (u64)handle, 0x100u, (u64)&mode, 4u);
}

static s32 virtual_pad_register(void)
{
    u8 scratch[0x114];

    memory_zero(scratch, sizeof(scratch));
    g_ds.pad_handle = -1;
    debug_register_pad(scratch, &g_ds.pad_handle);
    sysUsleep(10000);

    if (g_ds.pad_handle < 0) {
        ds_diag_info("DualSense Fix: registro avanzado no disponible; usando LDD estandar", 0);
        g_ds.pad_handle = ioPadLddRegisterController();
    } else {
        debug_enable_game_insertion(g_ds.pad_handle);
    }
    if (g_ds.pad_handle < 0) {
        ds_diag_error("DualSense Fix: no se pudo registrar el pad virtual",
                      g_ds.pad_handle, 1);
        return g_ds.pad_handle;
    }
    return 0;
}

static void virtual_pad_unregister(void)
{
    if (g_ds.pad_handle >= 0) {
        ioPadLddUnregisterController(g_ds.pad_handle);
        g_ds.pad_handle = -1;
    }
}

static void pipes_close(void)
{
    if (g_ds.input_pipe >= 0) {
        usbClosePipe(g_ds.input_pipe);
        g_ds.input_pipe = -1;
    }
    if (g_ds.output_pipe >= 0) {
        usbClosePipe(g_ds.output_pipe);
        g_ds.output_pipe = -1;
    }
    if (g_ds.control_pipe >= 0) {
        usbClosePipe(g_ds.control_pipe);
        g_ds.control_pipe = -1;
    }
}

static void report_free(void)
{
    if (g_ds.report) {
        usbFreeMemory(g_ds.report);
        g_ds.report = 0;
    }
}

static void dpad_map(u8 hat, u16 *digital1)
{
    switch (hat & 0x0fu) {
        case 0: *digital1 |= PAD_CTRL_UP; break;
        case 1: *digital1 |= PAD_CTRL_UP | PAD_CTRL_RIGHT; break;
        case 2: *digital1 |= PAD_CTRL_RIGHT; break;
        case 3: *digital1 |= PAD_CTRL_RIGHT | PAD_CTRL_DOWN; break;
        case 4: *digital1 |= PAD_CTRL_DOWN; break;
        case 5: *digital1 |= PAD_CTRL_DOWN | PAD_CTRL_LEFT; break;
        case 6: *digital1 |= PAD_CTRL_LEFT; break;
        case 7: *digital1 |= PAD_CTRL_LEFT | PAD_CTRL_UP; break;
        default: break;
    }
}

static void report_translate(const u8 *report, padData *pad)
{
    u8 buttons0 = report[8];
    u8 buttons1 = report[9];
    u8 buttons2 = report[10];
    u16 digital1 = 0;
    u16 digital2 = 0;

    memory_zero(pad, sizeof(*pad));
    pad->len = DS_PAD_LENGTH;
    pad->button[PAD_BUTTON_OFFSET_ANALOG_LEFT_X] = report[1];
    pad->button[PAD_BUTTON_OFFSET_ANALOG_LEFT_Y] = report[2];
    pad->button[PAD_BUTTON_OFFSET_ANALOG_RIGHT_X] = report[3];
    pad->button[PAD_BUTTON_OFFSET_ANALOG_RIGHT_Y] = report[4];
    dpad_map(buttons0, &digital1);

    if (buttons0 & (1u << 4)) digital2 |= PAD_CTRL_SQUARE;
    if (buttons0 & (1u << 5)) digital2 |= PAD_CTRL_CROSS;
    if (buttons0 & (1u << 6)) digital2 |= PAD_CTRL_CIRCLE;
    if (buttons0 & (1u << 7)) digital2 |= PAD_CTRL_TRIANGLE;
    if (buttons1 & (1u << 0)) digital2 |= PAD_CTRL_L1;
    if (buttons1 & (1u << 1)) digital2 |= PAD_CTRL_R1;
    if (buttons1 & (1u << 2)) digital2 |= PAD_CTRL_L2;
    if (buttons1 & (1u << 3)) digital2 |= PAD_CTRL_R2;
    if (buttons1 & (1u << 4)) digital1 |= PAD_CTRL_SELECT;
    if (buttons1 & (1u << 5)) digital1 |= PAD_CTRL_START;
    if (buttons1 & (1u << 6)) digital1 |= PAD_CTRL_L3;
    if (buttons1 & (1u << 7)) digital1 |= PAD_CTRL_R3;

    pad->button[PAD_BUTTON_OFFSET_DIGITAL1] = digital1;
    pad->button[PAD_BUTTON_OFFSET_DIGITAL2] = digital2;
    pad->button[PAD_BUTTON_OFFSET_PRESS_L2] = report[5];
    pad->button[PAD_BUTTON_OFFSET_PRESS_R2] = report[6];

    /* DualSense USB: raw byte 10, bit 0 is PS/Home. */
    pad->button[0] = (buttons2 & 0x01u) ? 0x0001u : 0u;
}

static s32 input_queue(void)
{
    if (!g_ds.attached || g_ds.input_pipe < 0 || !g_ds.report) {
        return -1;
    }
    return usbInterruptTransfer(g_ds.input_pipe, g_ds.report, DS_REPORT_SIZE,
                                input_done, &g_ds);
}

static void input_done(s32 result, s32 count, void *arg)
{
    ds_context *context = (ds_context *)arg;
    padData pad;
    u32 insert_result;
    s32 queue_result;

    if (!context || !context->attached) {
        return;
    }
    if (result != 0) {
        if (!context->transfer_error_reported) {
            ds_diag_error("DualSense Fix: fallo leyendo el informe USB", result, 1);
            context->transfer_error_reported = 1;
        }
    } else if (count >= 11 && context->report[0] == DS_REPORT_ID) {
        context->transfer_error_reported = 0;
        report_translate(context->report, &pad);
        insert_result = ioPadLddDataInsert(context->pad_handle, &pad);
        if (insert_result != 0 && !context->insert_error_reported) {
            ds_diag_error("DualSense Fix: fallo insertando datos del pad virtual",
                          (s32)insert_result, 1);
            context->insert_error_reported = 1;
        } else if (insert_result == 0) {
            context->insert_error_reported = 0;
        }
    }

    queue_result = input_queue();
    if (queue_result != 0 && !context->transfer_error_reported) {
        ds_diag_error("DualSense Fix: no se pudo rearmar la lectura USB",
                      queue_result, 1);
        context->transfer_error_reported = 1;
    }
}

static void interface_done(s32 result, s32 count, void *arg)
{
    ds_context *context = (ds_context *)arg;
    s32 queue_result;
    (void)count;

    if (!context || !context->attached) return;
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo seleccionar la interfaz USB",
                      result, 1);
        return;
    }
    queue_result = input_queue();
    if (queue_result != 0) {
        ds_diag_error("DualSense Fix: no se pudo iniciar la lectura USB",
                      queue_result, 1);
        return;
    }
    ds_diag_info("DualSense Fix: mando listo; boton PS habilitado", 1);
}

static void configuration_done(s32 result, s32 count, void *arg)
{
    ds_context *context = (ds_context *)arg;
    s32 interface_result;
    (void)count;

    if (!context || !context->attached) return;
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo configurar el DualSense USB",
                      result, 1);
        return;
    }
    if (context->alternate_setting == 0) {
        interface_done(0, 0, context);
        return;
    }
    interface_result = usbSetInterface(context->control_pipe,
        context->interface_number, context->alternate_setting,
        interface_done, context);
    if (interface_result != 0) {
        ds_diag_error("DualSense Fix: fallo al solicitar la interfaz USB",
                      interface_result, 1);
    }
}

static s32 ds_probe(s32 device_id)
{
    usbDeviceDescriptor *device = (usbDeviceDescriptor *)
        usbScanStaticDescriptor(device_id, 0, USB_DESCRIPTOR_TYPE_DEVICE);
    if (g_ds.attached || !device) return USB_PROBE_FAILED;
    return SWAP16(device->idVendor) == DUALSENSE_VENDOR_ID &&
           SWAP16(device->idProduct) == DUALSENSE_PRODUCT_ID
        ? USB_PROBE_SUCCEEDED : USB_PROBE_FAILED;
}

static s32 ds_attach(s32 device_id)
{
    usbConfigDescriptor *configuration;
    usbInterfaceDescriptor *interface;
    usbEndpointDescriptor *endpoint;
    usbEndpointDescriptor *input_endpoint = 0;
    usbEndpointDescriptor *output_endpoint = 0;
    void *cursor;
    s32 result;

    if (g_ds.attached) return USB_ATTACH_FAILED;
    context_reset();
    g_ds.device_id = device_id;

    configuration = (usbConfigDescriptor *)usbScanStaticDescriptor(
        device_id, 0, USB_DESCRIPTOR_TYPE_CONFIG);
    if (!configuration) {
        ds_diag_error("DualSense Fix: descriptor de configuracion ausente", -1, 1);
        return USB_ATTACH_FAILED;
    }
    interface = (usbInterfaceDescriptor *)usbScanStaticDescriptor(
        device_id, configuration, USB_DESCRIPTOR_TYPE_INTERFACE);
    if (!interface || interface->bInterfaceClass != USB_CLASS_HID) {
        ds_diag_error("DualSense Fix: interfaz HID del DualSense no encontrada", -1, 1);
        return USB_ATTACH_FAILED;
    }

    cursor = interface;
    while ((endpoint = (usbEndpointDescriptor *)usbScanStaticDescriptor(
            device_id, cursor, USB_DESCRIPTOR_TYPE_ENDPOINT)) != 0) {
        u8 direction;
        cursor = endpoint;
        if ((endpoint->bmAttributes & USB_ENDPOINT_TRANSFER_TYPE_BITS) !=
            USB_ENDPOINT_TRANSFER_TYPE_INTERRUPT) continue;
        direction = endpoint->bEndpointAddress & USB_ENDPOINT_DIRECTION_BITS;
        if (direction == USB_ENDPOINT_DIRECTION_IN && !input_endpoint)
            input_endpoint = endpoint;
        if (direction == USB_ENDPOINT_DIRECTION_OUT && !output_endpoint)
            output_endpoint = endpoint;
        if (input_endpoint && output_endpoint) break;
    }
    if (!input_endpoint) {
        ds_diag_error("DualSense Fix: endpoint USB de entrada no encontrado", -1, 1);
        return USB_ATTACH_FAILED;
    }

    g_ds.configuration = configuration->bConfigurationValue;
    g_ds.interface_number = interface->bInterfaceNumber;
    g_ds.alternate_setting = interface->bAlternateSetting;
    g_ds.control_pipe = usbOpenPipe(device_id, 0);
    g_ds.input_pipe = usbOpenPipe(device_id, input_endpoint);
    if (output_endpoint) g_ds.output_pipe = usbOpenPipe(device_id, output_endpoint);
    if (g_ds.control_pipe < 0 || g_ds.input_pipe < 0) {
        ds_diag_error("DualSense Fix: no se pudieron abrir los pipes USB", -1, 1);
        pipes_close();
        return USB_ATTACH_FAILED;
    }
    result = usbAllocateMemory((void **)&g_ds.report, DS_REPORT_SIZE);
    if (result != 0 || !g_ds.report) {
        ds_diag_error("DualSense Fix: no se pudo reservar el buffer USB", result, 1);
        pipes_close();
        return USB_ATTACH_FAILED;
    }
    result = virtual_pad_register();
    if (result != 0) {
        report_free();
        pipes_close();
        return USB_ATTACH_FAILED;
    }

    g_ds.attached = 1;
    usbSetPrivateData(device_id, &g_ds);
    ds_diag_info("DualSense Fix: DualSense USB detectado", 1);
    result = usbSetConfiguration(g_ds.control_pipe, g_ds.configuration,
                                 configuration_done, &g_ds);
    if (result != 0) {
        ds_diag_error("DualSense Fix: fallo al solicitar la configuracion USB",
                      result, 1);
        g_ds.attached = 0;
        usbSetPrivateData(device_id, 0);
        virtual_pad_unregister();
        report_free();
        pipes_close();
        return USB_ATTACH_FAILED;
    }
    return USB_ATTACH_SUCCEEDED;
}

static s32 ds_detach(s32 device_id)
{
    ds_context *context = (ds_context *)usbGetPrivateData(device_id);
    if (!context) return USB_DETACH_FAILED;
    context->attached = 0;
    usbSetPrivateData(device_id, 0);
    virtual_pad_unregister();
    pipes_close();
    report_free();
    ds_diag_info("DualSense Fix: DualSense desconectado", 1);
    context_reset();
    return USB_DETACH_SUCCEEDED;
}

s32 dualsense_usb_init(void)
{
    s32 result;
    context_reset();

    result = sysModuleLoad(SYSMODULE_USB);
    if (result != 0 && (u32)result != SYSMODULE_ERR_DUPLICATE) {
        ds_diag_error("DualSense Fix: no se pudo cargar el modulo USB", result, 1);
        return result;
    }
    g_module_loaded = result == 0;
    result = usbInit();
    if (result != 0 && (u32)result != USB_ERR_ALREADY_INITIALIZED) {
        ds_diag_error("DualSense Fix: no se pudo iniciar el subsistema USB", result, 1);
        if (g_module_loaded) sysModuleUnload(SYSMODULE_USB);
        g_module_loaded = 0;
        return result;
    }
    g_usb_initialized = result == 0;
    result = usbRegisterExtraLdd(&g_driver, DUALSENSE_VENDOR_ID,
                                 DUALSENSE_PRODUCT_ID);
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo registrar el driver DualSense", result, 1);
        if (g_usb_initialized) usbEnd();
        if (g_module_loaded) sysModuleUnload(SYSMODULE_USB);
        g_usb_initialized = 0;
        g_module_loaded = 0;
        return result;
    }
    g_driver_registered = 1;
    ds_diag_info("DualSense Fix: plugin cargado; esperando DualSense USB", 1);
    return 0;
}

void dualsense_usb_shutdown(void)
{
    if (g_ds.attached) ds_detach(g_ds.device_id);
    if (g_driver_registered) usbUnregisterExtraLdd(&g_driver);
    if (g_usb_initialized) usbEnd();
    if (g_module_loaded) sysModuleUnload(SYSMODULE_USB);
    g_driver_registered = 0;
    g_usb_initialized = 0;
    g_module_loaded = 0;
}

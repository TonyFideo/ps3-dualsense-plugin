#include <cell/pad.h>
#include <cell/usbd.h>
#include <stddef.h>
#include <sys/sys_time.h>
#include <sys/syscall.h>

#include "diagnostics.h"
#include "dualsense_usb.h"

#define DS_INPUT_REPORT_SIZE 64u
#define DS_INPUT_REPORT_ID 0x01u
#define DS_OUTPUT_REPORT_SIZE 63u
#define DS_OUTPUT_REPORT_ID 0x02u
#define DS_PAD_LENGTH 24
#define DS_INSERT_GAME_MODE 1u
#define DS_CAPABILITIES ((1u << 0) | (1u << 3))
#define DS_MAX_CONFIGURATION_LENGTH 4096u
#define DS_CONFIGURATION_DESCRIPTOR_LENGTH 9u
#define DS_INTERFACE_DESCRIPTOR_LENGTH 9u
#define DS_ENDPOINT_DESCRIPTOR_LENGTH 7u
#define DS_ENDPOINT_PACKET_SIZE_MASK 0x07ffu
#define DS_STATS_WINDOW_NANOSECONDS 1000000000u
#define BIT(value) (1u << (value))
#define SWAP16(value) ((u16)((((u16)(value) & 0xffu) << 8) | \
                             (((u16)(value) >> 8) & 0xffu)))

typedef enum ds_led_state {
    DS_LED_DISABLED = 0,
    DS_LED_IDLE,
    DS_LED_PREPARING,
    DS_LED_SETTING_BLUE,
    DS_LED_CONFIGURED,
    DS_LED_FAILED
} ds_led_state;

typedef struct __attribute__((packed)) ds_output_report_common {
    u8 valid_flag0;
    u8 valid_flag1;
    u8 motor_right;
    u8 motor_left;
    u8 headphone_volume;
    u8 speaker_volume;
    u8 microphone_volume;
    u8 audio_enable_bits;
    u8 mute_button_led;
    u8 power_save_control;
    u8 right_trigger_effect[11];
    u8 left_trigger_effect[11];
    u8 reserved[6];
    u8 valid_flag2;
    u8 effect_strength;
    u8 reserved2;
    u8 lightbar_setup;
    u8 led_brightness;
    u8 player_leds;
    u8 lightbar_red;
    u8 lightbar_green;
    u8 lightbar_blue;
} ds_output_report_common;

typedef struct __attribute__((packed)) ds_output_report_usb {
    u8 report_id;
    ds_output_report_common common;
    u8 reserved[15];
} ds_output_report_usb;

typedef char ds_output_report_common_size_check[
    (sizeof(ds_output_report_common) == 47u) ? 1 : -1];
typedef char ds_output_report_usb_size_check[
    (sizeof(ds_output_report_usb) == DS_OUTPUT_REPORT_SIZE) ? 1 : -1];

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
    u8 input_error_reported;
    u8 insert_error_reported;
    u8 led_error_reported;
    u8 first_inserted;
    u8 stats_window_logged;
    u8 stats_started;
    u8 input_callback_logged;
    ds_led_state led_state;
    u8 *input_buffer;
    u8 *output_buffer;
    u32 completed_reports;
    u32 valid_reports;
    u32 successful_inserts;
    u32 usb_errors;
    u32 insert_errors;
    sys_time_sec_t stats_start_seconds;
    sys_time_nsec_t stats_start_nanoseconds;
} ds_context;

static ds_context g_ds;
static u8 g_input_buffer[DS_INPUT_REPORT_SIZE] __attribute__((aligned(16)));
static u8 g_output_buffer[DS_OUTPUT_REPORT_SIZE] __attribute__((aligned(16)));
static s32 g_driver_registered;
static u8 g_shutdown_in_progress;

static s32 ds_probe(s32 device_id);
static s32 ds_attach(s32 device_id);
static s32 ds_detach(s32 device_id);
static void configuration_done(s32 result, s32 count, void *arg);
static void interface_done(s32 result, s32 count, void *arg);
static void input_done(s32 result, s32 count, void *arg);
static void output_done(s32 result, s32 count, void *arg);

static CellUsbdLddOps g_driver = {
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
    g_ds.led_state = DS_LED_DISABLED;
    g_ds.input_buffer = g_input_buffer;
}

static s32 debug_register_pad(u8 *scratch, s32 *handle)
{
    system_call_4(574, (u64)(uintptr_t)scratch, (u64)(uintptr_t)handle, 5u,
                  (u64)(DS_CAPABILITIES << 1));
    return (s32)p1;
}

static s32 debug_enable_game_insertion(s32 handle)
{
    u32 mode = DS_INSERT_GAME_MODE;

    system_call_4(573, (u64)handle, 0x100u, (u64)(uintptr_t)&mode, 4u);
    return (s32)p1;
}

static void virtual_pad_unregister(void)
{
    if (g_ds.pad_handle >= 0) {
        cellPadLddUnregisterController(g_ds.pad_handle);
        g_ds.pad_handle = -1;
    }
}

static s32 virtual_pad_register(void)
{
    u8 scratch[0x114];
    s32 register_result;
    s32 insert_mode_result = -1;
    s32 debug_handle = -1;
    u8 advanced_used = 0;
    u8 fallback_used = 0;

    memory_zero(scratch, sizeof(scratch));
    ds_diag_trace("LDD virtual: antes de syscall 574");
    register_result = debug_register_pad(scratch, &debug_handle);
    ds_diag_trace_value("LDD virtual: syscall 574 retorno", register_result);
    ds_diag_trace_value("LDD virtual: handle syscall 574", debug_handle);
    g_ds.pad_handle = debug_handle;

    if (register_result == 0 && debug_handle >= 0) {
        ds_diag_trace("LDD virtual: antes de syscall 573");
        insert_mode_result = debug_enable_game_insertion(debug_handle);
        ds_diag_trace_value("LDD virtual: syscall 573 retorno", insert_mode_result);
        if (insert_mode_result == 0) {
            advanced_used = 1;
        } else {
            virtual_pad_unregister();
        }
    }
    if (!advanced_used) {
        virtual_pad_unregister();
        fallback_used = 1;
        ds_diag_trace("LDD virtual: antes de cellPadLddRegisterController fallback");
        g_ds.pad_handle = cellPadLddRegisterController();
        ds_diag_trace_value("LDD virtual: handle fallback", g_ds.pad_handle);
    }

    ds_diag_ldd_registration(register_result, debug_handle, insert_mode_result,
                             advanced_used, fallback_used);
    if (g_ds.pad_handle < 0) {
        ds_diag_error("DualSense Fix: no se pudo registrar el pad virtual",
                      g_ds.pad_handle, 1);
        return g_ds.pad_handle;
    }
    return 0;
}

static void pipes_close(void)
{
    if (g_ds.input_pipe >= 0) {
        cellUsbdClosePipe(g_ds.input_pipe);
        g_ds.input_pipe = -1;
    }
    if (g_ds.output_pipe >= 0) {
        cellUsbdClosePipe(g_ds.output_pipe);
        g_ds.output_pipe = -1;
    }
    if (g_ds.control_pipe >= 0) {
        cellUsbdClosePipe(g_ds.control_pipe);
        g_ds.control_pipe = -1;
    }
}

static void buffers_release(void)
{
    g_ds.input_buffer = 0;
    g_ds.output_buffer = 0;
}

static void pipes_forget(void)
{
    g_ds.input_pipe = -1;
    g_ds.output_pipe = -1;
    g_ds.control_pipe = -1;
}

static void stats_start(ds_context *context)
{
    if (!context || sys_time_get_current_time(&context->stats_start_seconds,
                                      &context->stats_start_nanoseconds) != 0) {
        return;
    }
    context->stats_started = 1;
}

static u64 stats_elapsed_nanoseconds(const ds_context *context)
{
    sys_time_sec_t seconds;
    sys_time_nsec_t nanoseconds;
    u64 elapsed_seconds;

    if (!context || !context->stats_started ||
        sys_time_get_current_time(&seconds, &nanoseconds) != 0 ||
        seconds < context->stats_start_seconds) {
        return 0;
    }
    elapsed_seconds = seconds - context->stats_start_seconds;
    if (nanoseconds >= context->stats_start_nanoseconds) {
        return (elapsed_seconds * 1000000000u) +
               (nanoseconds - context->stats_start_nanoseconds);
    }
    if (elapsed_seconds == 0u) {
        return 0;
    }
    return ((elapsed_seconds - 1u) * 1000000000u) +
           (1000000000u + nanoseconds - context->stats_start_nanoseconds);
}

static void stats_report(ds_context *context, const char *reason)
{
    if (!context) {
        return;
    }
    ds_diag_counters(reason, context->completed_reports, context->valid_reports,
                     context->successful_inserts, context->usb_errors,
                     context->insert_errors, stats_elapsed_nanoseconds(context));
}

static void stats_report_window(ds_context *context)
{
    if (!context || context->stats_window_logged ||
        stats_elapsed_nanoseconds(context) < DS_STATS_WINDOW_NANOSECONDS) {
        return;
    }
    context->stats_window_logged = 1;
    stats_report(context, "primer segundo");
}

static void dpad_map(u8 hat, u16 *digital1)
{
    switch (hat & 0x0fu) {
        case 0: *digital1 |= CELL_PAD_CTRL_UP; break;
        case 1: *digital1 |= CELL_PAD_CTRL_UP | CELL_PAD_CTRL_RIGHT; break;
        case 2: *digital1 |= CELL_PAD_CTRL_RIGHT; break;
        case 3: *digital1 |= CELL_PAD_CTRL_RIGHT | CELL_PAD_CTRL_DOWN; break;
        case 4: *digital1 |= CELL_PAD_CTRL_DOWN; break;
        case 5: *digital1 |= CELL_PAD_CTRL_DOWN | CELL_PAD_CTRL_LEFT; break;
        case 6: *digital1 |= CELL_PAD_CTRL_LEFT; break;
        case 7: *digital1 |= CELL_PAD_CTRL_LEFT | CELL_PAD_CTRL_UP; break;
        default: break;
    }
}

static void report_translate(const u8 *report, CellPadData *pad)
{
    u8 buttons0 = report[8];
    u8 buttons1 = report[9];
    u8 buttons2 = report[10];
    u16 digital1 = 0;
    u16 digital2 = 0;

    memory_zero(pad, sizeof(*pad));
    pad->len = DS_PAD_LENGTH;
    pad->button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X] = report[1];
    pad->button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y] = report[2];
    pad->button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X] = report[3];
    pad->button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y] = report[4];
    dpad_map(buttons0, &digital1);

    if (buttons0 & BIT(4)) digital2 |= CELL_PAD_CTRL_SQUARE;
    if (buttons0 & BIT(5)) digital2 |= CELL_PAD_CTRL_CROSS;
    if (buttons0 & BIT(6)) digital2 |= CELL_PAD_CTRL_CIRCLE;
    if (buttons0 & BIT(7)) digital2 |= CELL_PAD_CTRL_TRIANGLE;
    if (buttons1 & BIT(0)) digital2 |= CELL_PAD_CTRL_L1;
    if (buttons1 & BIT(1)) digital2 |= CELL_PAD_CTRL_R1;
    if (buttons1 & BIT(2)) digital2 |= CELL_PAD_CTRL_L2;
    if (buttons1 & BIT(3)) digital2 |= CELL_PAD_CTRL_R2;
    if (buttons1 & BIT(4)) digital1 |= CELL_PAD_CTRL_SELECT;
    if (buttons1 & BIT(5)) digital1 |= CELL_PAD_CTRL_START;
    if (buttons1 & BIT(6)) digital1 |= CELL_PAD_CTRL_L3;
    if (buttons1 & BIT(7)) digital1 |= CELL_PAD_CTRL_R3;

    pad->button[CELL_PAD_BTN_OFFSET_DIGITAL1] = digital1;
    pad->button[CELL_PAD_BTN_OFFSET_DIGITAL2] = digital2;
    pad->button[CELL_PAD_BTN_OFFSET_PRESS_L2] = report[5];
    pad->button[CELL_PAD_BTN_OFFSET_PRESS_R2] = report[6];

    /* DualSense USB: raw byte 10, bit 0 is PS/Home. */
    pad->button[0] = (buttons2 & BIT(0)) ? CELL_PAD_CTRL_LDD_PS : 0u;
}

static s32 input_queue(void)
{
    if (!g_ds.attached || g_ds.input_pipe < 0 || !g_ds.input_buffer) {
        return -1;
    }
    return cellUsbdInterruptTransfer(g_ds.input_pipe, g_ds.input_buffer,
                                DS_INPUT_REPORT_SIZE, input_done, &g_ds);
}

static void led_transfer_failed(ds_context *context, s32 result)
{
    if (!context) {
        return;
    }
    context->usb_errors++;
    context->led_state = DS_LED_FAILED;
    if (!context->led_error_reported) {
        ds_diag_error("DualSense Fix: fallo configurando la luz azul", result, 1);
        context->led_error_reported = 1;
    }
}

static s32 led_queue_prepare(ds_context *context)
{
    ds_output_report_usb *report;
    s32 result;

    if (!context || !context->attached || context->output_pipe < 0 ||
        !context->output_buffer) {
        return -1;
    }
    memory_zero(context->output_buffer, DS_OUTPUT_REPORT_SIZE);
    report = (ds_output_report_usb *)context->output_buffer;
    report->report_id = DS_OUTPUT_REPORT_ID;
    report->common.valid_flag2 |= BIT(1);
    report->common.lightbar_setup = BIT(1);
    context->led_state = DS_LED_PREPARING;
    result = cellUsbdInterruptTransfer(context->output_pipe, context->output_buffer,
                                  DS_OUTPUT_REPORT_SIZE, output_done, context);
    if (result != 0) {
        led_transfer_failed(context, result);
    }
    return result;
}

static s32 led_queue_blue(ds_context *context)
{
    ds_output_report_usb *report;
    s32 result;

    if (!context || !context->attached || context->output_pipe < 0 ||
        !context->output_buffer) {
        return -1;
    }
    memory_zero(context->output_buffer, DS_OUTPUT_REPORT_SIZE);
    report = (ds_output_report_usb *)context->output_buffer;
    report->report_id = DS_OUTPUT_REPORT_ID;
    report->common.valid_flag1 |= BIT(2);
    report->common.lightbar_red = 0;
    report->common.lightbar_green = 0;
    report->common.lightbar_blue = 128u;
    context->led_state = DS_LED_SETTING_BLUE;
    result = cellUsbdInterruptTransfer(context->output_pipe, context->output_buffer,
                                  DS_OUTPUT_REPORT_SIZE, output_done, context);
    if (result != 0) {
        led_transfer_failed(context, result);
    }
    return result;
}

static void output_done(s32 result, s32 count, void *arg)
{
    ds_context *context = (ds_context *)arg;

    ds_diag_trace_value("USB callback OUT: result", result);
    ds_diag_trace_value("USB callback OUT: count", count);
    if (!context || !context->attached) {
        return;
    }
    if (result != 0 || count != (s32)DS_OUTPUT_REPORT_SIZE) {
        led_transfer_failed(context, result != 0 ? result : -1);
        stats_report_window(context);
        return;
    }
    if (context->led_state == DS_LED_PREPARING) {
        led_queue_blue(context);
    } else if (context->led_state == DS_LED_SETTING_BLUE) {
        context->led_state = DS_LED_CONFIGURED;
        ds_diag_info("DualSense: barra luminosa azul configurada", 0);
        ds_diag_info("DualSense: luz azul configurada", 1);
    }
    stats_report_window(context);
}

static void input_done(s32 result, s32 count, void *arg)
{
    ds_context *context = (ds_context *)arg;
    CellPadData pad;
    s32 insert_result;
    s32 queue_result;

    if (!context || !context->attached) {
        return;
    }
    if (!context->input_callback_logged) {
        ds_diag_trace_value("USB primer callback IN: result", result);
        ds_diag_trace_value("USB primer callback IN: count", count);
        context->input_callback_logged = 1;
    }
    context->completed_reports++;
    if (result != 0) {
        context->usb_errors++;
        if (!context->input_error_reported) {
            ds_diag_error("DualSense Fix: fallo leyendo el informe USB", result, 1);
            context->input_error_reported = 1;
        }
    } else if (count >= (s32)DS_INPUT_REPORT_SIZE &&
               context->input_buffer[0] == DS_INPUT_REPORT_ID) {
        context->input_error_reported = 0;
        context->valid_reports++;
        report_translate(context->input_buffer, &pad);
        insert_result = cellPadLddDataInsert(context->pad_handle, &pad);
        if (insert_result != 0) {
            context->insert_errors++;
            if (!context->insert_error_reported) {
                ds_diag_error("DualSense Fix: fallo insertando datos del pad virtual",
                              (s32)insert_result, 1);
                context->insert_error_reported = 1;
            }
        } else {
            context->successful_inserts++;
            context->insert_error_reported = 0;
            if (!context->first_inserted) {
                context->first_inserted = 1;
                ds_diag_info("DualSense listo: mando y boton PS", 1);
            }
        }
    }

    stats_report_window(context);
    queue_result = input_queue();
    if (queue_result != 0 && !context->input_error_reported) {
        context->usb_errors++;
        ds_diag_error("DualSense Fix: no se pudo rearmar la lectura USB",
                      queue_result, 1);
        context->input_error_reported = 1;
    }
}

static void interface_done(s32 result, s32 count, void *arg)
{
    ds_context *context = (ds_context *)arg;
    s32 queue_result;
    (void)count;

    ds_diag_trace_value("USB callback SetInterface: result", result);
    if (!context || !context->attached) {
        return;
    }
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo seleccionar la interfaz USB",
                      result, 1);
        return;
    }
    stats_start(context);
    queue_result = input_queue();
    if (queue_result != 0) {
        context->usb_errors++;
        ds_diag_error("DualSense Fix: no se pudo iniciar la lectura USB",
                      queue_result, 1);
        context->input_error_reported = 1;
    }
    if (context->led_state == DS_LED_IDLE) {
        led_queue_prepare(context);
    }
}

static void configuration_done(s32 result, s32 count, void *arg)
{
    ds_context *context = (ds_context *)arg;
    s32 interface_result;
    (void)count;

    ds_diag_trace_value("USB callback SetConfiguration: result", result);
    if (!context || !context->attached) {
        return;
    }
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo configurar el DualSense USB",
                      result, 1);
        return;
    }
    if (context->alternate_setting == 0u) {
        ds_diag_trace("USB callback SetConfiguration: interfaz alt 0, continua directo");
        interface_done(0, 0, context);
        return;
    }
    ds_diag_trace("USB callback SetConfiguration: antes de cellUsbdSetInterface");
    interface_result = cellUsbdSetInterface(context->control_pipe,
                                       context->interface_number,
                                       context->alternate_setting,
                                       interface_done, context);
    ds_diag_trace_value("USB callback SetConfiguration: cellUsbdSetInterface retorno",
                        interface_result);
    if (interface_result != 0) {
        ds_diag_error("DualSense Fix: fallo al solicitar la interfaz USB",
                      interface_result, 1);
    }
}

static s32 ds_probe(s32 device_id)
{
    s32 probe_result;
    UsbDeviceDescriptor *device = (UsbDeviceDescriptor *)
        cellUsbdScanStaticDescriptor(device_id, 0, USB_DESCRIPTOR_TYPE_DEVICE);

    ds_diag_trace_value("USB probe: device_id", device_id);
    if (g_ds.attached || !device) {
        ds_diag_trace("USB probe: rechazado por contexto ocupado o descriptor ausente");
        return CELL_USBD_PROBE_FAILED;
    }
    probe_result = SWAP16(device->idVendor) == DUALSENSE_VENDOR_ID &&
                   SWAP16(device->idProduct) == DUALSENSE_PRODUCT_ID
        ? CELL_USBD_PROBE_SUCCEEDED : CELL_USBD_PROBE_FAILED;
    ds_diag_trace_value("USB probe: resultado", probe_result);
    return probe_result;
}

static s32 ds_attach(s32 device_id)
{
    UsbConfigurationDescriptor *configuration;
    UsbInterfaceDescriptor *hid_interface = 0;
    UsbEndpointDescriptor *input_endpoint = 0;
    UsbEndpointDescriptor *output_endpoint = 0;
    const u8 *cursor;
    const u8 *configuration_end;
    u32 configuration_length;
    u8 hid_active = 0;
    u8 hid_found = 0;
    u8 speed = 0;
    u16 input_packet_size;
    u16 output_packet_size = 0;
    s32 speed_result;
    s32 result;

    ds_diag_trace_value("USB attach: device_id", device_id);
    if (g_ds.attached) {
        ds_diag_trace("USB attach: rechazado porque ya existe un DualSense");
        return CELL_USBD_ATTACH_FAILED;
    }
    context_reset();
    g_ds.device_id = device_id;

    configuration = (UsbConfigurationDescriptor *)cellUsbdScanStaticDescriptor(
        device_id, 0, USB_DESCRIPTOR_TYPE_CONFIGURATION);
    if (!configuration) {
        ds_diag_error("DualSense Fix: descriptor de configuracion ausente", -1, 1);
        return CELL_USBD_ATTACH_FAILED;
    }
    ds_diag_trace("USB attach: descriptor de configuracion encontrado");
    configuration_length = SWAP16(configuration->wTotalLength);
    ds_diag_trace_value("USB attach: configuration bLength",
                        configuration->bLength);
    if (configuration->bLength < DS_CONFIGURATION_DESCRIPTOR_LENGTH ||
        configuration_length < configuration->bLength ||
        configuration_length > DS_MAX_CONFIGURATION_LENGTH) {
        ds_diag_error("DualSense Fix: longitud de configuracion USB invalida", -1, 1);
        return CELL_USBD_ATTACH_FAILED;
    }

    cursor = (const u8 *)configuration;
    configuration_end = cursor + configuration_length;
    while (cursor < configuration_end) {
        u32 remaining = (u32)(configuration_end - cursor);
        u8 descriptor_length;
        u8 descriptor_type;

        if (remaining < 2u) {
            ds_diag_error("DualSense Fix: descriptor USB truncado", -1, 1);
            return CELL_USBD_ATTACH_FAILED;
        }
        descriptor_length = cursor[0];
        descriptor_type = cursor[1];
        if (descriptor_length < 2u || (u32)descriptor_length > remaining) {
            ds_diag_error("DualSense Fix: descriptor USB invalido", -1, 1);
            return CELL_USBD_ATTACH_FAILED;
        }

        if (descriptor_type == USB_DESCRIPTOR_TYPE_INTERFACE) {
            UsbInterfaceDescriptor *interface = (UsbInterfaceDescriptor *)cursor;

            ds_diag_trace_value("USB attach: interface bLength", descriptor_length);
            if (descriptor_length < DS_INTERFACE_DESCRIPTOR_LENGTH) {
                ds_diag_error("DualSense Fix: interfaz USB invalida", -1, 1);
                return CELL_USBD_ATTACH_FAILED;
            }
            if (hid_active && input_endpoint) {
                hid_found = 1;
                hid_active = 0;
            }
            if (!hid_found) {
                hid_active = 0;
                input_endpoint = 0;
                output_endpoint = 0;
                hid_interface = 0;
                if (interface->bInterfaceClass == USB_CLASS_HID) {
                    hid_active = 1;
                    hid_interface = interface;
                }
            }
        } else if (descriptor_type == USB_DESCRIPTOR_TYPE_ENDPOINT && hid_active) {
            UsbEndpointDescriptor *endpoint = (UsbEndpointDescriptor *)cursor;
            u8 direction;
            u16 packet_size;

            ds_diag_trace_value("USB attach: endpoint bLength", descriptor_length);
            if (descriptor_length < DS_ENDPOINT_DESCRIPTOR_LENGTH) {
                ds_diag_error("DualSense Fix: endpoint USB invalido", -1, 1);
                return CELL_USBD_ATTACH_FAILED;
            }
            if ((endpoint->bmAttributes & USB_ENDPOINT_TRANSFER_TYPE_BITS) ==
                USB_ENDPOINT_TRANSFER_TYPE_INTERRUPT) {
                direction = endpoint->bEndpointAddress & USB_ENDPOINT_DIRECTION_BITS;
                packet_size = SWAP16(endpoint->wMaxPacketSize) &
                              DS_ENDPOINT_PACKET_SIZE_MASK;
                if (direction == USB_ENDPOINT_DIRECTION_IN && !input_endpoint &&
                    packet_size >= (u16)DS_INPUT_REPORT_SIZE) {
                    input_endpoint = endpoint;
                } else if (direction == USB_ENDPOINT_DIRECTION_OUT &&
                           !output_endpoint) {
                    output_endpoint = endpoint;
                }
            }
        }
        cursor += descriptor_length;
    }
    if (hid_active && input_endpoint) {
        hid_found = 1;
    }
    if (!hid_found || !hid_interface || !input_endpoint) {
        ds_diag_error("DualSense Fix: interfaz HID con entrada de 64 bytes no encontrada",
                      -1, 1);
        return CELL_USBD_ATTACH_FAILED;
    }
    ds_diag_trace("USB attach: interfaz HID y endpoint IN encontrados");

    input_packet_size = SWAP16(input_endpoint->wMaxPacketSize) &
                        DS_ENDPOINT_PACKET_SIZE_MASK;
    if (output_endpoint) {
        output_packet_size = SWAP16(output_endpoint->wMaxPacketSize) &
                             DS_ENDPOINT_PACKET_SIZE_MASK;
    }
    speed_result = cellUsbdGetDeviceSpeed(device_id, &speed);
    ds_diag_trace_value("USB attach: cellUsbdGetDeviceSpeed retorno", speed_result);
    ds_diag_usb_layout(configuration->bConfigurationValue,
                       configuration->bNumInterfaces,
                       hid_interface->bInterfaceNumber,
                       hid_interface->bAlternateSetting,
                       input_endpoint->bEndpointAddress, input_packet_size,
                       input_endpoint->bInterval,
                       output_endpoint ? output_endpoint->bEndpointAddress : -1,
                       output_packet_size,
                       output_endpoint ? output_endpoint->bInterval : 0u,
                       speed_result, speed);

    g_ds.configuration = configuration->bConfigurationValue;
    g_ds.interface_number = hid_interface->bInterfaceNumber;
    g_ds.alternate_setting = hid_interface->bAlternateSetting;
    g_ds.control_pipe = cellUsbdOpenPipe(device_id, 0);
    ds_diag_trace_value("USB attach: control pipe", g_ds.control_pipe);
    g_ds.input_pipe = cellUsbdOpenPipe(device_id, input_endpoint);
    ds_diag_trace_value("USB attach: input pipe", g_ds.input_pipe);
    if (output_endpoint) {
        g_ds.output_pipe = cellUsbdOpenPipe(device_id, output_endpoint);
        ds_diag_trace_value("USB attach: output pipe", g_ds.output_pipe);
    }
    if (g_ds.control_pipe < 0 || g_ds.input_pipe < 0) {
        ds_diag_error("DualSense Fix: no se pudieron abrir los pipes USB", -1, 1);
        pipes_close();
        return CELL_USBD_ATTACH_FAILED;
    }
    memory_zero(g_ds.input_buffer, DS_INPUT_REPORT_SIZE);
    if (output_endpoint && g_ds.output_pipe >= 0) {
        g_ds.output_buffer = g_output_buffer;
        memory_zero(g_ds.output_buffer, DS_OUTPUT_REPORT_SIZE);
        g_ds.led_state = DS_LED_IDLE;
    } else if (!output_endpoint) {
        ds_diag_info("DualSense Fix: luz azul no disponible; endpoint OUT ausente", 0);
    } else {
        ds_diag_error("DualSense Fix: luz azul no disponible; pipe OUT", g_ds.output_pipe, 0);
    }
    ds_diag_trace("USB attach: antes de registrar pad virtual");
    result = virtual_pad_register();
    ds_diag_trace_value("USB attach: registro de pad virtual retorno", result);
    if (result != 0) {
        buffers_release();
        pipes_close();
        return CELL_USBD_ATTACH_FAILED;
    }

    g_ds.attached = 1;
    cellUsbdSetPrivateData(device_id, &g_ds);
    ds_diag_trace("USB attach: private data establecido");
    ds_diag_info("DualSense USB detectado", 1);
    ds_diag_trace("USB attach: antes de cellUsbdSetConfiguration");
    result = cellUsbdSetConfiguration(g_ds.control_pipe, g_ds.configuration,
                                 configuration_done, &g_ds);
    ds_diag_trace_value("USB attach: cellUsbdSetConfiguration retorno", result);
    if (result != 0) {
        ds_diag_error("DualSense Fix: fallo al solicitar la configuracion USB",
                      result, 1);
        g_ds.attached = 0;
        cellUsbdSetPrivateData(device_id, 0);
        virtual_pad_unregister();
        buffers_release();
        pipes_close();
        return CELL_USBD_ATTACH_FAILED;
    }
    return CELL_USBD_ATTACH_SUCCEEDED;
}

static s32 ds_detach(s32 device_id)
{
    ds_context *context = (ds_context *)cellUsbdGetPrivateData(device_id);

    ds_diag_trace_value("USB detach: device_id", device_id);
    if (!context) {
        return CELL_USBD_DETACH_FAILED;
    }
    /* Invalidate callbacks before libusbd closes the detached device pipes. */
    context->attached = 0;
    cellUsbdSetPrivateData(device_id, 0);
    stats_report(context, g_shutdown_in_progress ? "detencion" : "desconexion");
    virtual_pad_unregister();
    if (g_shutdown_in_progress) {
        pipes_close();
    } else {
        pipes_forget();
    }
    buffers_release();
    ds_diag_info("DualSense Fix: DualSense desconectado", 1);
    context_reset();
    ds_diag_trace("USB detach: completo");
    return CELL_USBD_DETACH_SUCCEEDED;
}

s32 dualsense_usb_init(void)
{
    s32 result;

    ds_diag_trace("USB init: entrada");
    context_reset();
    ds_diag_trace("USB init: contexto reiniciado");
    ds_diag_trace("USB init: VSH ya administra libusbd; se omite inicializacion global");
    ds_diag_trace("USB init: antes de cellUsbdRegisterExtraLdd");
    result = cellUsbdRegisterExtraLdd(&g_driver, DUALSENSE_VENDOR_ID,
                                 DUALSENSE_PRODUCT_ID);
    ds_diag_trace_value("USB init: cellUsbdRegisterExtraLdd retorno", result);
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo registrar el driver DualSense", result, 1);
        return result;
    }
    g_driver_registered = 1;
    ds_diag_trace("USB init: driver registrado");
    ds_diag_info("DualSense Fix: plugin cargado; esperando DualSense USB", 1);
    return 0;
}

void dualsense_usb_shutdown(void)
{
    ds_diag_trace("USB shutdown: entrada");
    g_shutdown_in_progress = 1;
    if (g_ds.attached) {
        ds_detach(g_ds.device_id);
    }
    if (g_driver_registered) {
        ds_diag_trace("USB shutdown: antes de cellUsbdUnregisterExtraLdd");
        cellUsbdUnregisterExtraLdd(&g_driver);
        ds_diag_trace("USB shutdown: cellUsbdUnregisterExtraLdd completo");
    }
    g_driver_registered = 0;
    g_shutdown_in_progress = 0;
    ds_diag_trace("USB shutdown: completo");
}

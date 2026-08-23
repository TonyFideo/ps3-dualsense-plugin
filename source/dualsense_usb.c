#include <io/pad.h>
#include <ppu-lv2.h>
#include <sys/systime.h>
#include <sysmodule/sysmodule.h>
#include <usb/usb.h>

#include "diagnostics.h"
#include "dualsense_usb.h"
#include "usb_descriptor_parser.h"

#define DS_INPUT_REPORT_SIZE 64u
#define DS_INPUT_REPORT_ID 0x01u
#define DS_OUTPUT_REPORT_SIZE 63u
#define DS_OUTPUT_REPORT_ID 0x02u
#define DS_PAD_LENGTH 24
#define DS_INSERT_GAME_MODE 1u
#define DS_CAPABILITIES ((1u << 0) | (1u << 3))
#define DS_MAX_CONFIGURATION_LENGTH 4096u
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

_Static_assert(sizeof(ds_output_report_common) == 47u,
               "DualSense output payload must be 47 bytes");
_Static_assert(sizeof(ds_output_report_usb) == DS_OUTPUT_REPORT_SIZE,
               "DualSense USB output report must be 63 bytes");

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
    ds_led_state led_state;
    u8 *input_buffer;
    u8 *output_buffer;
    u32 completed_reports;
    u32 valid_reports;
    u32 successful_inserts;
    u32 usb_errors;
    u32 insert_errors;
    u64 stats_start_seconds;
    u64 stats_start_nanoseconds;
} ds_context;

static ds_context g_ds;
static s32 g_module_loaded;
static s32 g_usb_initialized;
static s32 g_driver_registered;
static u8 g_shutdown_in_progress;

static s32 ds_probe(s32 device_id);
static s32 ds_attach(s32 device_id);
static s32 ds_detach(s32 device_id);
static void configuration_done(s32 result, s32 count, void *arg);
static void interface_done(s32 result, s32 count, void *arg);
static void input_done(s32 result, s32 count, void *arg);
static void output_done(s32 result, s32 count, void *arg);

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
    g_ds.led_state = DS_LED_DISABLED;
}

static s32 debug_register_pad(u8 *scratch, s32 *handle)
{
    lv2syscall4(574, (u64)scratch, (u64)handle, 5u,
                (u64)(DS_CAPABILITIES << 1));
    return (s32)p1;
}

static s32 debug_enable_game_insertion(s32 handle)
{
    u32 mode = DS_INSERT_GAME_MODE;

    lv2syscall4(573, (u64)handle, 0x100u, (u64)&mode, 4u);
    return (s32)p1;
}

static void virtual_pad_unregister(void)
{
    if (g_ds.pad_handle >= 0) {
        ioPadLddUnregisterController(g_ds.pad_handle);
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
    register_result = debug_register_pad(scratch, &debug_handle);
    g_ds.pad_handle = debug_handle;

    if (register_result == 0 && debug_handle >= 0) {
        /* PS3XPAD waits for LV2 to finish publishing the new LDD handle. */
        sysUsleep(10000u);
        insert_mode_result = debug_enable_game_insertion(debug_handle);
        if (insert_mode_result == 0) {
            advanced_used = 1;
        } else {
            virtual_pad_unregister();
        }
    }
    if (!advanced_used) {
        virtual_pad_unregister();
        fallback_used = 1;
        g_ds.pad_handle = ioPadLddRegisterController();
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

static void buffers_free(void)
{
    if (g_ds.input_buffer) {
        usbFreeMemory(g_ds.input_buffer);
        g_ds.input_buffer = 0;
    }
    if (g_ds.output_buffer) {
        usbFreeMemory(g_ds.output_buffer);
        g_ds.output_buffer = 0;
    }
}

static void stats_start(ds_context *context)
{
    if (!context || sysGetCurrentTime(&context->stats_start_seconds,
                                      &context->stats_start_nanoseconds) != 0) {
        return;
    }
    context->stats_started = 1;
}

static u64 stats_elapsed_nanoseconds(const ds_context *context)
{
    u64 seconds;
    u64 nanoseconds;
    u64 elapsed_seconds;

    if (!context || !context->stats_started ||
        sysGetCurrentTime(&seconds, &nanoseconds) != 0 ||
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

    if (buttons0 & BIT(4)) digital2 |= PAD_CTRL_SQUARE;
    if (buttons0 & BIT(5)) digital2 |= PAD_CTRL_CROSS;
    if (buttons0 & BIT(6)) digital2 |= PAD_CTRL_CIRCLE;
    if (buttons0 & BIT(7)) digital2 |= PAD_CTRL_TRIANGLE;
    if (buttons1 & BIT(0)) digital2 |= PAD_CTRL_L1;
    if (buttons1 & BIT(1)) digital2 |= PAD_CTRL_R1;
    if (buttons1 & BIT(2)) digital2 |= PAD_CTRL_L2;
    if (buttons1 & BIT(3)) digital2 |= PAD_CTRL_R2;
    if (buttons1 & BIT(4)) digital1 |= PAD_CTRL_SELECT;
    if (buttons1 & BIT(5)) digital1 |= PAD_CTRL_START;
    if (buttons1 & BIT(6)) digital1 |= PAD_CTRL_L3;
    if (buttons1 & BIT(7)) digital1 |= PAD_CTRL_R3;

    pad->button[PAD_BUTTON_OFFSET_DIGITAL1] = digital1;
    pad->button[PAD_BUTTON_OFFSET_DIGITAL2] = digital2;
    pad->button[PAD_BUTTON_OFFSET_PRESS_L2] = report[5];
    pad->button[PAD_BUTTON_OFFSET_PRESS_R2] = report[6];

    /* DualSense USB: raw byte 10, bit 0 is PS/Home. */
    pad->button[0] = (buttons2 & BIT(0)) ? 0x0001u : 0u;
}

static s32 input_queue(void)
{
    if (!g_ds.attached || g_ds.input_pipe < 0 || !g_ds.input_buffer) {
        return -1;
    }
    return usbInterruptTransfer(g_ds.input_pipe, g_ds.input_buffer,
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
    result = usbInterruptTransfer(context->output_pipe, context->output_buffer,
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
    result = usbInterruptTransfer(context->output_pipe, context->output_buffer,
                                  DS_OUTPUT_REPORT_SIZE, output_done, context);
    if (result != 0) {
        led_transfer_failed(context, result);
    }
    return result;
}

static void output_done(s32 result, s32 count, void *arg)
{
    ds_context *context = (ds_context *)arg;

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
    padData pad;
    u32 insert_result;
    s32 queue_result;

    if (!context || !context->attached) {
        return;
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
        insert_result = ioPadLddDataInsert(context->pad_handle, &pad);
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

    if (!context || !context->attached) {
        return;
    }
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo configurar el DualSense USB",
                      result, 1);
        return;
    }
    if (context->alternate_setting == 0u) {
        interface_done(0, 0, context);
        return;
    }
    interface_result = usbSetInterface(context->control_pipe,
                                       context->interface_number,
                                       context->alternate_setting,
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

    if (g_ds.attached || !device) {
        return USB_PROBE_FAILED;
    }
    return SWAP16(device->idVendor) == DUALSENSE_VENDOR_ID &&
           SWAP16(device->idProduct) == DUALSENSE_PRODUCT_ID
        ? USB_PROBE_SUCCEEDED : USB_PROBE_FAILED;
}

static s32 ds_attach(s32 device_id)
{
    const u8 *configuration;
    usbEndpointDescriptor *input_endpoint;
    usbEndpointDescriptor *output_endpoint;
    ds_usb_layout layout;
    u32 configuration_length;
    u8 speed = 0;
    s32 parse_result;
    s32 speed_result;
    s32 result;

    if (g_ds.attached) {
        return USB_ATTACH_FAILED;
    }
    context_reset();
    g_ds.device_id = device_id;

    configuration = (const u8 *)usbScanStaticDescriptor(
        device_id, 0, USB_DESCRIPTOR_TYPE_CONFIG);
    if (!configuration) {
        ds_diag_error("DualSense Fix: descriptor de configuracion ausente", -1, 1);
        return USB_ATTACH_FAILED;
    }
    if (configuration[0] < 4u) {
        ds_diag_error("DualSense Fix: descriptor de configuracion invalido", -1, 1);
        return USB_ATTACH_FAILED;
    }
    configuration_length = ds_usb_read_le16(configuration + 2u);
    if (configuration_length < configuration[0] ||
        configuration_length > DS_MAX_CONFIGURATION_LENGTH) {
        ds_diag_error("DualSense Fix: longitud de configuracion USB invalida", -1, 1);
        return USB_ATTACH_FAILED;
    }
    parse_result = ds_usb_parse_configuration(configuration,
                                              configuration_length, &layout);
    if (parse_result != DS_USB_PARSE_OK) {
        ds_diag_error("DualSense Fix: interfaz HID con entrada de 64 bytes no encontrada",
                      parse_result, 1);
        return USB_ATTACH_FAILED;
    }

    input_endpoint = (usbEndpointDescriptor *)layout.input_endpoint;
    output_endpoint = (usbEndpointDescriptor *)layout.output_endpoint;
    speed_result = usbGetDeviceSpeed(device_id, &speed);
    ds_diag_usb_layout(layout.configuration_value, layout.interface_count,
                       layout.interface_number, layout.alternate_setting,
                       layout.input_address, layout.input_packet_size,
                       layout.input_interval,
                       output_endpoint ? layout.output_address : -1,
                       layout.output_packet_size,
                       layout.output_interval,
                       speed_result, speed);

    g_ds.configuration = layout.configuration_value;
    g_ds.interface_number = layout.interface_number;
    g_ds.alternate_setting = layout.alternate_setting;
    g_ds.control_pipe = usbOpenPipe(device_id, 0);
    g_ds.input_pipe = usbOpenPipe(device_id, input_endpoint);
    if (output_endpoint) {
        g_ds.output_pipe = usbOpenPipe(device_id, output_endpoint);
    }
    if (g_ds.control_pipe < 0 || g_ds.input_pipe < 0) {
        ds_diag_error("DualSense Fix: no se pudieron abrir los pipes USB", -1, 1);
        pipes_close();
        return USB_ATTACH_FAILED;
    }
    result = usbAllocateMemory((void **)&g_ds.input_buffer, DS_INPUT_REPORT_SIZE);
    if (result != 0 || !g_ds.input_buffer) {
        ds_diag_error("DualSense Fix: no se pudo reservar el buffer de entrada USB",
                      result, 1);
        buffers_free();
        pipes_close();
        return USB_ATTACH_FAILED;
    }
    if (output_endpoint && g_ds.output_pipe >= 0) {
        result = usbAllocateMemory((void **)&g_ds.output_buffer,
                                   DS_OUTPUT_REPORT_SIZE);
        if (result == 0 && g_ds.output_buffer) {
            g_ds.led_state = DS_LED_IDLE;
        } else {
            ds_diag_error("DualSense Fix: luz azul no disponible; buffer OUT", result, 0);
            if (g_ds.output_buffer) {
                usbFreeMemory(g_ds.output_buffer);
                g_ds.output_buffer = 0;
            }
            usbClosePipe(g_ds.output_pipe);
            g_ds.output_pipe = -1;
        }
    } else if (!output_endpoint) {
        ds_diag_info("DualSense Fix: luz azul no disponible; endpoint OUT ausente", 0);
    } else {
        ds_diag_error("DualSense Fix: luz azul no disponible; pipe OUT", g_ds.output_pipe, 0);
    }
    result = virtual_pad_register();
    if (result != 0) {
        buffers_free();
        pipes_close();
        return USB_ATTACH_FAILED;
    }

    g_ds.attached = 1;
    usbSetPrivateData(device_id, &g_ds);
    ds_diag_info("DualSense USB detectado", 1);
    result = usbSetConfiguration(g_ds.control_pipe, g_ds.configuration,
                                 configuration_done, &g_ds);
    if (result != 0) {
        ds_diag_error("DualSense Fix: fallo al solicitar la configuracion USB",
                      result, 1);
        g_ds.attached = 0;
        usbSetPrivateData(device_id, 0);
        virtual_pad_unregister();
        buffers_free();
        pipes_close();
        return USB_ATTACH_FAILED;
    }
    return USB_ATTACH_SUCCEEDED;
}

static s32 ds_detach(s32 device_id)
{
    ds_context *context = (ds_context *)usbGetPrivateData(device_id);

    if (!context) {
        return USB_DETACH_FAILED;
    }
    /* Invalidate callbacks before their pipes and USB-owned buffers disappear. */
    context->attached = 0;
    usbSetPrivateData(device_id, 0);
    stats_report(context, g_shutdown_in_progress ? "detencion" : "desconexion");
    virtual_pad_unregister();
    pipes_close();
    buffers_free();
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
        if (g_module_loaded) {
            sysModuleUnload(SYSMODULE_USB);
        }
        g_module_loaded = 0;
        return result;
    }
    g_usb_initialized = result == 0;
    result = usbRegisterExtraLdd(&g_driver, DUALSENSE_VENDOR_ID,
                                 DUALSENSE_PRODUCT_ID);
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo registrar el driver DualSense", result, 1);
        if (g_usb_initialized) {
            usbEnd();
        }
        if (g_module_loaded) {
            sysModuleUnload(SYSMODULE_USB);
        }
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
    g_shutdown_in_progress = 1;
    if (g_ds.attached) {
        ds_detach(g_ds.device_id);
    }
    if (g_driver_registered) {
        usbUnregisterExtraLdd(&g_driver);
    }
    if (g_usb_initialized) {
        usbEnd();
    }
    if (g_module_loaded) {
        sysModuleUnload(SYSMODULE_USB);
    }
    g_driver_registered = 0;
    g_usb_initialized = 0;
    g_module_loaded = 0;
    g_shutdown_in_progress = 0;
}

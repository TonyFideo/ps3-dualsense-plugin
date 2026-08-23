#include <cell/fs/cell_fs_file_api.h>
#include <stdint.h>
#include <sys/sys_time.h>

#include "diagnostics.h"

#define VSHTASK_NOTIFY_NID 0xa02d46e7u
#define VSH_EXPORT_TABLE_OFFSET 0x984u
#define VSH_EXPORT_TABLE_POINTER 0x1008cu
#define VSH_EXPORT_SCAN_LIMIT 4096u
#define LOG_LINE_SIZE 512u

typedef s32 (*vsh_notify_fn)(s32 icon, const char *message);

static vsh_notify_fn g_vsh_notify;

static s32 string_equals(const char *left, const char *right)
{
    u32 i = 0;
    if (!left || !right) {
        return 0;
    }
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        i++;
    }
    return left[i] == right[i];
}

static u32 append_text(char *target, u32 offset, u32 capacity, const char *text)
{
    u32 i = 0;
    if (!text) {
        return offset;
    }
    while (text[i] != '\0' && offset + 1u < capacity) {
        target[offset++] = text[i++];
    }
    target[offset] = '\0';
    return offset;
}

static u32 append_hex64(char *target, u32 offset, u32 capacity, u64 value)
{
    static const char digits[] = "0123456789abcdef";
    s32 shift;
    offset = append_text(target, offset, capacity, "0x");
    for (shift = 60; shift >= 0 && offset + 1u < capacity; shift -= 4) {
        target[offset++] = digits[(value >> shift) & 0x0fu];
    }
    target[offset] = '\0';
    return offset;
}

static u32 append_decimal64(char *target, u32 offset, u32 capacity, u64 value)
{
    char digits[20];
    u32 count = 0;

    if (value == 0u) {
        return append_text(target, offset, capacity, "0");
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u && offset + 1u < capacity) {
        target[offset++] = digits[--count];
    }
    target[offset] = '\0';
    return offset;
}

static u32 append_decimal_s32(char *target, u32 offset, u32 capacity, s32 value)
{
    if (value < 0) {
        offset = append_text(target, offset, capacity, "-");
        return append_decimal64(target, offset, capacity, (u64)(-(s64)value));
    }
    return append_decimal64(target, offset, capacity, (u64)value);
}

static u32 append_hex8(char *target, u32 offset, u32 capacity, u8 value)
{
    static const char digits[] = "0123456789abcdef";

    offset = append_text(target, offset, capacity, "0x");
    if (offset + 3u < capacity) {
        target[offset++] = digits[(value >> 4) & 0x0fu];
        target[offset++] = digits[value & 0x0fu];
        target[offset] = '\0';
    }
    return offset;
}

static void *find_vsh_export(const char *library, u32 fnid)
{
    u32 table_base;
    u32 table;
    u32 slot;

    /* Export-table lookup used by PS3XPAD/webMAN MOD. It is bounded and
       fails closed when this is not a normal VSH address layout. */
    table_base = *(volatile u32 *)(uintptr_t)VSH_EXPORT_TABLE_POINTER;
    if (table_base < 0x10000u || table_base > 0x0f000000u) {
        return 0;
    }

    table = table_base + VSH_EXPORT_TABLE_OFFSET;
    for (slot = 0; slot < VSH_EXPORT_SCAN_LIMIT; slot++, table += 4u) {
        u32 export_address = *(volatile u32 *)(uintptr_t)table;
        u32 *export_table;
        const char *name;
        u32 *fnids;
        u32 *functions;
        u32 name_address;
        u32 fnid_address;
        u32 function_address;
        u16 count;
        u16 i;

        if (export_address == 0u) {
            break;
        }
        if (export_address < 0x10000u || export_address > 0x0f000000u) {
            continue;
        }

        export_table = (u32 *)(uintptr_t)export_address;
        name_address = export_table[4];
        fnid_address = export_table[5];
        function_address = export_table[6];
        if (name_address < 0x10000u || name_address > 0x0f000000u ||
            fnid_address < 0x10000u || fnid_address > 0x0f000000u ||
            function_address < 0x10000u || function_address > 0x0f000000u) {
            continue;
        }
        name = (const char *)(uintptr_t)name_address;
        if (!string_equals(library, name)) {
            continue;
        }

        count = *(u16 *)((u8 *)export_table + 6u);
        if (count == 0 || count > 1024u) {
            return 0;
        }
        fnids = (u32 *)(uintptr_t)fnid_address;
        functions = (u32 *)(uintptr_t)function_address;
        for (i = 0; i < count; i++) {
            if (fnids[i] == fnid) {
                return (void *)(uintptr_t)functions[i];
            }
        }
    }
    return 0;
}

static void write_log_line(const char *level, const char *message, s32 has_error, s32 error_code)
{
    char line[LOG_LINE_SIZE];
    u32 length = 0;
    s32 fd = -1;
    u64 written = 0;
    sys_time_sec_t seconds = 0;
    sys_time_nsec_t nanoseconds = 0;

    line[0] = '\0';
    if (sys_time_get_current_time(&seconds, &nanoseconds) == 0) {
        length = append_text(line, length, LOG_LINE_SIZE, "[time=");
        length = append_hex64(line, length, LOG_LINE_SIZE, (u64)seconds);
        length = append_text(line, length, LOG_LINE_SIZE, "] ");
    }
    length = append_text(line, length, LOG_LINE_SIZE, "[");
    length = append_text(line, length, LOG_LINE_SIZE, level);
    length = append_text(line, length, LOG_LINE_SIZE, "] ");
    length = append_text(line, length, LOG_LINE_SIZE, message);
    if (has_error) {
        length = append_text(line, length, LOG_LINE_SIZE, " (error=");
        length = append_hex64(line, length, LOG_LINE_SIZE, (u32)error_code);
        length = append_text(line, length, LOG_LINE_SIZE, ")");
    }
    length = append_text(line, length, LOG_LINE_SIZE, "\n");

    if (cellFsOpen(DS_LOG_PATH, CELL_FS_O_WRONLY | CELL_FS_O_CREAT |
                   CELL_FS_O_APPEND, &fd, NULL, 0) == 0) {
        cellFsWrite(fd, line, length, &written);
        cellFsClose(fd);
    }
}

static void notify(const char *message)
{
    if (g_vsh_notify) {
        g_vsh_notify(0, message);
    }
}

void ds_diag_init(void)
{
    g_vsh_notify = (vsh_notify_fn)find_vsh_export("vshtask", VSHTASK_NOTIFY_NID);
    write_log_line("INFO", "--- inicio del plugin DualSense Fix ---", 0, 0);
    if (!g_vsh_notify) {
        write_log_line("WARN", "la exportacion de notificaciones VSH no esta disponible", 0, 0);
    }
}

void ds_diag_shutdown(void)
{
    write_log_line("INFO", "--- fin del plugin DualSense Fix ---", 0, 0);
    g_vsh_notify = 0;
}

void ds_diag_trace(const char *message)
{
    write_log_line("TRACE", message, 0, 0);
}

void ds_diag_trace_value(const char *message, s32 value)
{
    char line[LOG_LINE_SIZE];
    u32 length = 0;

    line[0] = '\0';
    length = append_text(line, length, sizeof(line), message);
    length = append_text(line, length, sizeof(line), " (value=");
    length = append_hex64(line, length, sizeof(line), (u32)value);
    append_text(line, length, sizeof(line), ")");
    write_log_line("TRACE", line, 0, 0);
}

void ds_diag_info(const char *message, s32 show_notification)
{
    write_log_line("INFO", message, 0, 0);
    if (show_notification) {
        notify(message);
    }
}

void ds_diag_error(const char *message, s32 error_code, s32 show_notification)
{
    write_log_line("ERROR", message, 1, error_code);
    if (show_notification) {
        notify(message);
    }
}

void ds_diag_usb_layout(u8 configuration, u8 interface_count,
                        u8 interface_number, u8 alternate_setting,
                        u8 input_address, u16 input_packet_size,
                        u8 input_interval, s32 output_address,
                        u16 output_packet_size, u8 output_interval,
                        s32 speed_result, u8 speed)
{
    char message[LOG_LINE_SIZE];
    u32 length = 0;

    message[0] = '\0';
    length = append_text(message, length, sizeof(message), "DualSense USB: cfg=");
    length = append_decimal64(message, length, sizeof(message), configuration);
    length = append_text(message, length, sizeof(message), " interfaces=");
    length = append_decimal64(message, length, sizeof(message), interface_count);
    length = append_text(message, length, sizeof(message), " HID=");
    length = append_decimal64(message, length, sizeof(message), interface_number);
    length = append_text(message, length, sizeof(message), " alt=");
    length = append_decimal64(message, length, sizeof(message), alternate_setting);
    length = append_text(message, length, sizeof(message), " IN=");
    length = append_hex8(message, length, sizeof(message), input_address);
    length = append_text(message, length, sizeof(message), "/");
    length = append_decimal64(message, length, sizeof(message), input_packet_size);
    length = append_text(message, length, sizeof(message), "/");
    length = append_decimal64(message, length, sizeof(message), input_interval);
    length = append_text(message, length, sizeof(message), " OUT=");
    if (output_address < 0) {
        length = append_text(message, length, sizeof(message), "ausente");
    } else {
        length = append_hex8(message, length, sizeof(message), (u8)output_address);
        length = append_text(message, length, sizeof(message), "/");
        length = append_decimal64(message, length, sizeof(message), output_packet_size);
        length = append_text(message, length, sizeof(message), "/");
        length = append_decimal64(message, length, sizeof(message), output_interval);
    }
    length = append_text(message, length, sizeof(message), " velocidad=");
    if (speed_result == 0) {
        length = append_decimal64(message, length, sizeof(message), speed);
    } else {
        length = append_text(message, length, sizeof(message), "no disponible (");
        length = append_decimal_s32(message, length, sizeof(message), speed_result);
        length = append_text(message, length, sizeof(message), ")");
    }
    ds_diag_info(message, 0);
}

void ds_diag_ldd_registration(s32 register_result, s32 register_handle,
                              s32 insert_mode_result, u8 advanced_used,
                              u8 fallback_used)
{
    char message[LOG_LINE_SIZE];
    u32 length = 0;

    message[0] = '\0';
    length = append_text(message, length, sizeof(message),
                         "DualSense LDD: syscall574=");
    length = append_decimal_s32(message, length, sizeof(message), register_result);
    length = append_text(message, length, sizeof(message), " handle=");
    length = append_decimal_s32(message, length, sizeof(message), register_handle);
    length = append_text(message, length, sizeof(message), " syscall573=");
    if (insert_mode_result < 0 && !advanced_used) {
        length = append_text(message, length, sizeof(message), "no ejecutado");
    } else {
        length = append_decimal_s32(message, length, sizeof(message),
                                    insert_mode_result);
    }
    length = append_text(message, length, sizeof(message), " registro=");
    length = append_text(message, length, sizeof(message),
                         advanced_used ? "avanzado" : "fallback");
    length = append_text(message, length, sizeof(message), " fallback=");
    length = append_text(message, length, sizeof(message), fallback_used ? "si" : "no");
    ds_diag_info(message, 0);
}

void ds_diag_counters(const char *reason, u32 completed_reports,
                      u32 valid_reports, u32 successful_inserts,
                      u32 usb_errors, u32 insert_errors,
                      u64 elapsed_nanoseconds)
{
    char message[LOG_LINE_SIZE];
    u32 length = 0;
    u64 reports_per_second = 0;

    if (elapsed_nanoseconds != 0u) {
        reports_per_second = ((u64)valid_reports * 1000000000u) /
                             elapsed_nanoseconds;
    }
    message[0] = '\0';
    length = append_text(message, length, sizeof(message), "DualSense USB estadisticas (");
    length = append_text(message, length, sizeof(message), reason);
    length = append_text(message, length, sizeof(message), "): completados=");
    length = append_decimal64(message, length, sizeof(message), completed_reports);
    length = append_text(message, length, sizeof(message), " validos_0x01=");
    length = append_decimal64(message, length, sizeof(message), valid_reports);
    length = append_text(message, length, sizeof(message), " inserciones=");
    length = append_decimal64(message, length, sizeof(message), successful_inserts);
    length = append_text(message, length, sizeof(message), " errores_usb=");
    length = append_decimal64(message, length, sizeof(message), usb_errors);
    length = append_text(message, length, sizeof(message), " errores_insercion=");
    length = append_decimal64(message, length, sizeof(message), insert_errors);
    length = append_text(message, length, sizeof(message), " tasa_aprox=");
    length = append_decimal64(message, length, sizeof(message), reports_per_second);
    append_text(message, length, sizeof(message), "/s");
    ds_diag_info(message, 0);
}

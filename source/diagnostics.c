#include <lv2/sysfs.h>
#include <stdint.h>
#include <sys/file.h>
#include <sys/systime.h>

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
    u64 seconds = 0;
    u64 nanoseconds = 0;

    line[0] = '\0';
    if (sysGetCurrentTime(&seconds, &nanoseconds) == 0) {
        length = append_text(line, length, LOG_LINE_SIZE, "[time=");
        length = append_hex64(line, length, LOG_LINE_SIZE, seconds);
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

    if (sysLv2FsOpen(DS_LOG_PATH, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND,
                     &fd, 0666, 0, 0) == 0) {
        sysLv2FsWrite(fd, line, length, &written);
        sysLv2FsClose(fd);
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

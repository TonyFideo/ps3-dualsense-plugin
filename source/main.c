#include "ds_types.h"
#include <sys/ppu_thread.h>
#include <sys/prx.h>

#include "diagnostics.h"
#include "dualsense_usb.h"

#define DS_WORKER_PRIORITY (-0x1d8)
#define DS_WORKER_STACK_SIZE 0x4000u
#define DS_WORKER_NAME "dualsense_usb"

s32 module_start(size_t args, void *argp);
s32 module_stop(size_t args, void *argp);

static void dualsense_worker(u64 arg)
{
    s32 result;

    (void)arg;
    ds_diag_trace("worker USB: entrada");
    result = dualsense_usb_init();
    ds_diag_trace_value("worker USB: dualsense_usb_init retorno", result);
    if (result != 0) {
        ds_diag_error("DualSense Fix: el worker USB no pudo inicializarse",
                      result, 1);
    }
    ds_diag_trace("worker USB: salida");
    sys_ppu_thread_exit((u64)(u32)result);
}

SYS_MODULE_INFO(DualSenseFix, 0, 1, 0);
SYS_MODULE_START(module_start);
SYS_MODULE_STOP(module_stop);

s32 module_start(size_t args, void *argp)
{
    sys_ppu_thread_t worker_id;
    s32 result;

    (void)args;
    (void)argp;
    ds_diag_init();
    ds_diag_trace("module_start: antes de crear worker USB");
    result = sys_ppu_thread_create(&worker_id, dualsense_worker, 0,
                                   DS_WORKER_PRIORITY, DS_WORKER_STACK_SIZE,
                                   0, DS_WORKER_NAME);
    ds_diag_trace_value("module_start: sys_ppu_thread_create retorno", result);
    if (result != 0) {
        ds_diag_error("DualSense Fix: no se pudo crear el worker USB", result, 1);
        return SYS_PRX_NO_RESIDENT;
    }
    ds_diag_trace("module_start: worker creado; termina hilo del cargador");
    sys_ppu_thread_exit(0);
    return SYS_PRX_RESIDENT;
}

s32 module_stop(size_t args, void *argp)
{
    (void)args;
    (void)argp;
    ds_diag_trace("module_stop: antes de dualsense_usb_shutdown");
    dualsense_usb_shutdown();
    ds_diag_trace("module_stop: dualsense_usb_shutdown completo");
    ds_diag_info("DualSense Fix: plugin detenido", 1);
    ds_diag_shutdown();
    return SYS_PRX_STOP_OK;
}

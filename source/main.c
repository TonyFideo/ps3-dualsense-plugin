#include <ppu-types.h>
#include <sys/prx.h>

#include "diagnostics.h"
#include "dualsense_usb.h"

s32 module_start(size_t args, void *argp);
s32 module_stop(size_t args, void *argp);

s32 module_start(size_t args, void *argp)
{
    s32 result;
    (void)args;
    (void)argp;
    ds_diag_init();
    result = dualsense_usb_init();
    if (result != 0) {
        ds_diag_error("DualSense Fix: el plugin no pudo inicializarse", result, 1);
        return SYS_PRX_NO_RESIDENT;
    }
    return SYS_PRX_RESIDENT;
}

s32 module_stop(size_t args, void *argp)
{
    (void)args;
    (void)argp;
    dualsense_usb_shutdown();
    ds_diag_info("DualSense Fix: plugin detenido", 1);
    ds_diag_shutdown();
    return SYS_PRX_STOP_OK;
}

#include <ppu-lv2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysmodule/sysmodule.h>

// Forward declarations if we don't have the headers
#ifndef CELL_USBD_PROBE_SUCCEEDED
#define CELL_USBD_PROBE_SUCCEEDED 0
#define CELL_USBD_PROBE_FAILED -1
#define CELL_USBD_ATTACH_SUCCEEDED 0
#define CELL_USBD_ATTACH_FAILED -1
#define CELL_USBD_DETACH_SUCCEEDED 0
#define CELL_USBD_DETACH_FAILED -1
#endif

// Entry point del VSH Plugin
int module_start(size_t args, void *argp)
{
    sysModuleLoad(SYSMODULE_USBD);
    
    // We will initialize threads here
    return 0;
}

int module_stop(size_t args, void *argp)
{
    sysModuleUnload(SYSMODULE_USBD);
    return 0;
}

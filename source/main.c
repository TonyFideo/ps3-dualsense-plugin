#include <ppu-lv2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dualsense_usb.h"

// Entry point del VSH Plugin
int module_start(size_t args, void *argp)
{
    // Initialize DualSense USB listener
    init_dualsense_usb();
    
    return 0;
}

int module_stop(size_t args, void *argp)
{
    // Shutdown and unregister
    shutdown_dualsense_usb();
    return 0;
}

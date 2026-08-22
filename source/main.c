#include <ppu-lv2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Entry point del VSH Plugin
int module_start(size_t args, void *argp)
{
    // Aquí inicializaremos los threads para usbd y el manejo de los paquetes HID
    return 0;
}

int module_stop(size_t args, void *argp)
{
    // Limpieza al salir
    return 0;
}

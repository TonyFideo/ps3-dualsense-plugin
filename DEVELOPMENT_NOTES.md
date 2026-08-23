# Notas de desarrollo

## Diagnóstico del código inicial

El repositorio era un esqueleto: registraba un LDD USB y un pad virtual, pero
no abría endpoints, no programaba transferencias, no analizaba informes HID y
no llamaba a `ioPadLddDataInsert`. Además mezclaba nombres que no existen en el
PSL1GHT actual (`CellUsbdLddOps`, `cellUsbd*`, `SYSMODULE_USBD`,
`-lusbd_stub`, `PRXGEN`).

## Flujo implementado

1. `usbRegisterExtraLdd` filtra Sony `054c:0ce6`.
2. `attach` descubre configuración, interfaz HID y endpoints interrupt IN/OUT.
3. Se crea un pad LDD; se intenta primero el modo de registro/inserción usado
   por PS3XPAD y se cae a `ioPadLddRegisterController` si no está disponible.
4. Una transferencia interrupt asíncrona de 64 bytes se rearma en cada callback.
5. El informe DualSense se traduce a `padData`; PS/Home (`report[10] & 1`) se
   inserta en `button[0]` como `0x0001`.
6. `detach` invalida callbacks, retira el pad, cierra pipes y libera memoria.

## SPRX con PSL1GHT

PSL1GHT no incluye los macros propietarios `SYS_MODULE_*` ni la opción GCC
`-mprx`. `source/prx_module.c` genera las tablas especiales `module_start` y
`module_stop` mediante sus NID conocidos y punteros PRX de 32 bits. El enlace
usa `-shared`, `lv2-sprx.o`, entrada cero y la cadena oficial:

`ppu-strip -> sprxlinker -> make_self`

La CI verifica que el PRX sea `ET_DYN`, tenga entrada `0x0` y conserve
`.sys_proc_prx_param`, `.lib.ent` y `.lib.stub`.

## Decisiones y límites

- Un DualSense USB en esta versión inicial.
- No se anuncia vibración hasta implementar entrada de actuadores y salida HID.
- El log solo recibe transiciones y el primer error de una ráfaga.
- Bluetooth requiere otra vía de captura/hook; no se presenta como compatible.

## Próximos hitos

1. Prueba real en CFW y HEN con el log de la consola.
2. Camino Bluetooth sin duplicar el pad genérico.
3. Actuadores LDD e informe USB de salida `0x02` para vibración.
4. DualSense Edge, varios mandos, LED, batería, touchpad y sensores.

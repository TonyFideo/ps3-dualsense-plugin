# Notas de desarrollo

## Diagnóstico del código inicial

El repositorio era un esqueleto: registraba un LDD USB y un pad virtual, pero
no abría endpoints, no programaba transferencias, no analizaba informes HID y
no llamaba a `ioPadLddDataInsert`. Además mezclaba nombres que no existen en el
PSL1GHT actual (`CellUsbdLddOps`, `cellUsbd*`, `SYSMODULE_USBD`,
`-lusbd_stub`, `PRXGEN`).

## Flujo implementado

1. `usbRegisterExtraLdd` filtra Sony `054c:0ce6`.
2. `attach` recorre de forma acotada `wTotalLength` de la configuración,
   valida cada descriptor y descubre por clase la interfaz HID y sus endpoints
   interrupt IN/OUT. No se fija la interfaz 3, aunque sea la habitual del
   DualSense estándar.
   El análisis está aislado en `usb_descriptor_parser.c`: usa los tamaños del
   protocolo USB, acepta endpoints estándar de 7 bytes y lee `wMaxPacketSize`
   sin accesos PPU desalineados.
3. Se crea un pad LDD; se intenta primero el modo de registro/inserción usado
   por PS3XPAD, incluyendo su espera de 10 ms antes del modo de inserción, y se
   cae a `ioPadLddRegisterController` si no está disponible.
4. Una transferencia interrupt asíncrona de 64 bytes se rearma en cada callback.
5. El informe DualSense se traduce a `padData`; PS/Home (`report[10] & 1`) se
   inserta en `button[0]` como `0x0001`.
6. La barra luminosa usa dos informes USB `0x02` asíncronos y separados del
   buffer de entrada: preparación y azul `0,0,128`. Si OUT no existe o falla,
   solo se desactiva el LED; la lectura y el LDD continúan.
7. `detach` invalida callbacks, registra contadores, retira el pad, cierra
   pipes y libera ambos buffers USB.

## Entorno y primera prueba

El objetivo inicial es PS3 4.93, CFW Evilnat Cobra 8.5, modo P-CEX y un
DualSense estándar Sony `054c:0ce6` por USB. Aún requiere validación física.
Las interfaces de audio permanecen en alternate setting 0; no hay soporte para
audio, micrófono, parlante, Bluetooth, vibración, touchpad ni sensores.

El registro se guarda en `/dev_hdd0/tmp/dualsense_fix.log`. `module_start`
escribe su primera línea antes de resolver notificaciones o inicializar USB;
por ello el archivo debe aparecer en el XMB incluso sin mando. La primera
prueba segura debe empezar sin PS3XPAD para el mismo VID/PID, verificar carga,
detección, primer `padData` insertado, botón PS y luz azul, y conservar un
método para arrancar sin la entrada del plugin si VSH falla.

## SPRX con PSL1GHT

PSL1GHT no incluye los macros propietarios `SYS_MODULE_*`, la opción GCC
`-mprx` ni `ppu-lv2-prx-strip`. `source/prx_module.c` genera `sceModuleInfo` y
las tablas especiales `module_start`/`module_stop` mediante sus NID conocidos
y punteros PRX de 32 bits.

La primera cadena usada (`-shared -> sprxlinker -> make_self`) producía un
`ET_DYN`, eliminaba `sceModuleInfo` mediante `--gc-sections` y envolvía el ELF
con el Auth-ID de aplicación retail. Cobra no llegó a ejecutar `module_start`:
en la prueba física no hubo log, notificación ni actividad USB.

La cadena corregida conserva PSL1GHT:

1. enlaza con `-Bsymbolic` para resolver localmente los stubs y retiene
   `sceModuleInfo` en el primer segmento;
2. `sprxlinker` ajusta las tablas PSL1GHT;
3. `tools/prxgen.py` convierte relocaciones PPC64 en registros de 24 bytes y
   emite `ET_SCE_PPURELA` (`0xffa4`) con `PT_SCE_PPURELA`;
4. `tools/validate_prx.py` emula las relocaciones y valida módulo/exportaciones;
5. `make_self.c` de PSL1GHT compilado con `-DSPRX` crea el SELF VSH, y
   `tools/validate_self.py` comprueba Auth-ID y PRX embebido.

PS3XPAD logra el mismo tipo de salida con el SDK propietario:
`ppu-lv2-gcc -mprx -> ppu-lv2-prx-strip -> scetool`. Aquí no se incorpora el
SDK propietario. La CI también ejecuta el fixture de configuración compuesta
audio + HID para impedir regresiones del descriptor endpoint USB de 7 bytes.

## Decisiones y límites

- Un DualSense USB en esta versión inicial.
- No se implementa vibración: la salida HID actual solo inicializa la luz azul.
- El log solo recibe transiciones y el primer error de una ráfaga.
- Bluetooth requiere otra vía de captura/hook; no se presenta como compatible.

## Próximos hitos

1. Prueba real en 4.93 Evilnat 8.5 P-CEX con el log de la consola.
2. Camino Bluetooth sin duplicar el pad genérico.
3. Actuadores LDD e informe USB de salida `0x02` para vibración.
4. DualSense Edge, varios mandos, LED, batería, touchpad y sensores.

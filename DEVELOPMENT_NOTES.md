# Notas de desarrollo

## Arquitectura actual

El plugin registra un LDD USB específico para el DualSense estándar Sony
`054c:0ce6` y expone sus informes como un `CellPadData` mediante un pad LDD
virtual. La implementación usa exclusivamente cabeceras, macros, herramientas
y librerías stub del SDK oficial de PS3.

## Flujo implementado

1. `cellUsbdRegisterExtraLdd` filtra el VID/PID del DualSense.
2. `attach` recorre de forma acotada `wTotalLength`, valida cada descriptor y
   descubre por clase la interfaz HID y sus endpoints interrupt IN/OUT.
3. Se registra un pad LDD. Primero se intenta el modo avanzado de inserción de
   juegos mediante los syscalls 574/573 usados por PS3XPAD; si no está
   disponible, se usa `cellPadLddRegisterController`.
4. Una transferencia `cellUsbdInterruptTransfer` de 64 bytes se rearma desde
   cada callback válido.
5. El informe se traduce a `CellPadData`; PS/Home (`report[10] & 1`) se inserta
   como `CELL_PAD_CTRL_LDD_PS`.
6. El endpoint OUT, cuando existe, recibe dos informes USB `0x02`: preparación
   de la barra luminosa y color azul `0,0,128`. Un fallo de salida no desactiva
   la entrada ni el pad LDD.
7. Los buffers de 64 y 63 bytes son estáticos. `cellUsbdAllocateMemory` no se
   usa porque la memoria rápida oficial solo acepta páginas desde 64 KB.
8. En una desconexión física, `detach` invalida callbacks y deja que libusbd
   cierre los pipes. Durante la parada explícita del plugin, los pipes sí se
   cierran antes de finalizar libusbd.

## Diagnóstico

El registro se guarda en `/dev_hdd0/tmp/dualsense_fix.log`. Las notificaciones
VSH son opcionales y se resuelven dinámicamente mediante la exportación
`vshtask`; su ausencia no impide usar el mando.

Los contadores distinguen informes completados, informes válidos, inserciones
correctas, errores USB y errores LDD. Se registra una ventana inicial de un
segundo y un resumen al desconectar o detener el plugin.

## PRX y SPRX con el SDK oficial

`source/main.c` declara el módulo con `SYS_MODULE_INFO`, `SYS_MODULE_START` y
`SYS_MODULE_STOP`. El sistema Make oficial compila las fuentes, enlaza
`dualsense_fix.sym`, copia `dualsense_fix.prx` y genera
`dualsense_fix.sprx` mediante `make_fself`.

Las dependencias enlazadas son:

- `libusbd_stub.a`
- `libio_stub.a`
- `libsysmodule_stub.a`
- `libfs_stub.a`
- `liblv2_stub.a`
- `libgcc.a`

La validación comprueba el tipo PRX CellOS `0xffa4`, entrada `0x0`, secciones
`.lib.ent`, `.lib.stub`, `.rodata.sceModuleInfo` y `.rodata.sceResident`, además
de los símbolos `module_start` y `module_stop`.

## Entorno y primera prueba

El objetivo inicial es PS3 4.93, CFW Evilnat Cobra 8.5, modo P-CEX y un
DualSense estándar por USB. Aún requiere validación física. Las interfaces de
audio permanecen en alternate setting 0; no hay soporte para audio, micrófono,
parlante, Bluetooth, vibración, touchpad ni sensores.

La primera prueba debe realizarse sin PS3XPAD reclamando el mismo VID/PID,
comprobar detección, primera inserción, botón PS y luz azul, y conservar un
método para iniciar VSH sin cargar el plugin.

## Decisiones y límites

- Un DualSense USB en esta versión inicial.
- El pad virtual se conserva para entregar datos a VSH y juegos.
- No se implementa vibración; el informe de salida solo configura la luz azul.
- El log evita repetir continuamente el mismo error.
- Bluetooth requiere una vía de captura diferente.

## Próximos hitos

1. Prueba real en 4.93 Evilnat 8.5 P-CEX con el log de la consola.
2. Compatibilidad controlada con otros plugins que registran LDD o USB.
3. Varios DualSense con un contexto y un pad LDD independiente por dispositivo.
4. Actuadores, batería, touchpad y sensores.

# DualSense Fix para PS3

Plugin VSH experimental para CFW/HEN que reclama un DualSense conectado por USB
(Sony `054c:0ce6`), crea un pad LDD de PS3 e inserta el botón PS que el soporte
genérico de PS3 omite.

## Estado actual

- Objetivo del MVP: DualSense estándar Sony `054c:0ce6` por **USB** y botón
  **PS**. Esta iteración está dirigida inicialmente a PS3 4.93 con Evilnat
  Cobra 8.5 en modo P-CEX, pero sigue pendiente de validación física.
- Traducción incluida: sticks, cruceta, botones frontales, L1/R1, L2/R2,
  Create/Options, L3/R3 y PS.
- La interfaz HID se busca dinámicamente por clase dentro de la configuración;
  no se fija el número de interfaz. Las interfaces de audio permanecen en
  alternate setting 0 y no se activan.
- El analizador usa los tamaños USB transmitidos (interfaz 9, endpoint 7) y
  decodifica los campos de 16 bits byte a byte, sin depender de la alineación
  o del `sizeof` del ABI PPU.
- La barra luminosa USB se configura en azul PlayStation al conectar, si el
  endpoint interrupt OUT está disponible. El mando sigue funcionando si no lo
  está o si falla esa transferencia.
- Bluetooth, vibración, audio, micrófono, parlante, touchpad y sensores:
  pendientes.
- Se recomienda probar primero en una consola de desarrollo/pruebas y conservar
  una forma de desactivar plugins de arranque.

## Diagnóstico

El plugin escribe estados y errores en:

`/dev_hdd0/tmp/dualsense_fix.log`

También intenta mostrar notificaciones breves del XMB. La resolución de la
exportación `vshtask` se basa en el mecanismo probado por PS3XPAD/webMAN MOD.
Si esa exportación no está disponible en una combinación concreta de
CFW/HEN/firmware, el plugin continúa sin notificaciones y deja constancia en el
log.

Las notificaciones cubren detección, primer informe LDD correcto, luz azul,
desconexión y el primer error de una ráfaga. Esto evita inundar la esquina
superior derecha si un endpoint falla repetidamente.

## Compilación local con PSL1GHT

Requiere la distribución de `ps3dev/ps3dev` que incluye el toolchain PPU,
PSL1GHT y `sprxlinker`. También necesita `make_sprx`: es el `make_self.c` de
PSL1GHT compilado con `-DSPRX`, que selecciona la identidad SELF de módulos
VSH. El `make_self` normal **no** sirve para un plugin de `boot_plugins.txt`.
La CI construye ese ejecutable desde una revisión y un tarball verificados.

```sh
export PS3DEV=/ruta/a/ps3dev
export PSL1GHT="$PS3DEV"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH"
# Compila tools/geohot/make_self.c de PSL1GHT con -DSPRX y deja el
# ejecutable resultante en "$PS3DEV/bin/make_sprx".
make clean all
```

Salidas:

- `dualsense_fix.prx`: PRX LV2 real `ET_SCE_PPURELA` (`0xffa4`), útil para
  inspección; no se instala directamente.
- `dualsense_fix.sprx`: plugin firmado para CFW/HEN.
- `build/dualsense_fix.map`: mapa de enlace para depuración.

## GitHub Actions

`.github/workflows/build.yml` descarga el release oficial precompilado y fijado
de `ps3dev/ps3dev`. El código C se compila con PSL1GHT. `tools/prxgen.py`
convierte las relocaciones ELF al segmento `SCE_PPURELA` que espera LV2, y el
modo SPRX del propio código abierto de PSL1GHT crea el SELF con Auth-ID de
módulo VSH. No se usa ningún archivo del SDK propietario.

La CI emula todas las relocaciones, valida `sceModuleInfo`, las exportaciones
`module_start`/`module_stop`, el tipo `0xffa4`, el Auth-ID
`1070000052000001` y el PRX embebido. Esto reemplaza la comprobación anterior
de `ET_DYN`, que aceptaba un artefacto que Cobra no podía cargar como plugin.

Antes de compilar para PPU también ejecuta `make test-host`. Esta prueba recorre
una configuración USB compuesta con audio seguido de HID y endpoints estándar
de 7 bytes, además de casos truncados y salida OUT opcional.

## Instalación de prueba

1. Descarga `dualsense_fix.sprx` del artefacto de GitHub Actions.
2. Copia el archivo a una ruta de plugins de la consola, por ejemplo
   `/dev_hdd0/plugins/dualsense_fix.sprx`.
3. Añádelo al mecanismo de carga de plugins de tu CFW/HEN (por ejemplo
   `boot_plugins.txt`) y reinicia VSH.
4. Reinicia y, todavía en el XMB, comprueba que exista
   `/dev_hdd0/tmp/dualsense_fix.log`. Debe contener `inicio del plugin` incluso
   sin conectar el mando. Esta es la prueba independiente de que Cobra ejecutó
   `module_start`.
5. Conecta solamente un DualSense estándar por USB; no conectes un DualSense
   Edge ni actives PS3XPAD para el mismo VID/PID.
6. Confirma las notificaciones “DualSense USB detectado”, “DualSense listo:
   mando y boton PS” y, cuando haya OUT, “DualSense: luz azul configurada”.
7. Prueba sticks, cruceta, botones, gatillos y PS sin salir del XMB. No
   pruebes Bluetooth, audio, micrófono, parlante, vibración, touchpad ni
   sensores en esta versión.
8. Si falla VSH o no responde, inicia sin cargar plugins, comenta o elimina la
   entrada de `boot_plugins.txt` y reinicia. Conserva una copia conocida buena
   del archivo y copia después
   `/dev_hdd0/tmp/dualsense_fix.log` antes de volver a probar.

La ruta y el cargador exactos dependen del CFW/HEN. Evita cargar a la vez
PS3XPAD para el mismo VID/PID: ambos intentarían reclamar el dispositivo.

## Base técnica

- PSL1GHT: APIs actuales `usb*`, `ioPadLdd*` y syscalls LV2 documentadas en sus
  headers.
- DualSense USB: informe `0x01` de 64 bytes; PS/Home es el bit 0 del tercer byte
  de botones (`raw report[10]`).
- PS3XPAD: patrón de LDD virtual, modo de inserción en juegos, transferencias
  USB asíncronas y resolución de la notificación VSH. PS3XPAD se construye con
  el SDK oficial (`ppu-lv2-gcc -mprx`, `ppu-lv2-prx-strip` y `scetool`); este
  proyecto reproduce el formato de salida mediante herramientas abiertas para
  conservar PSL1GHT como SDK de compilación.
- El informe de entrada es `0x01` de 64 bytes; PS/Home usa `report[10]` bit 0
  y se inserta en `padData.button[0]` como `0x0001`.
- El informe USB de salida `0x02` mide 63 bytes y se usa solamente para la
  secuencia asíncrona de preparación y color azul de la barra luminosa.

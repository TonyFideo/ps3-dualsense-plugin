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
PSL1GHT, `sprxlinker` y `make_self`. La CI está fijada al release verificado
`nightly-2026-07-26` para evitar cambios silenciosos del SDK.

```sh
export PS3DEV=/ruta/a/ps3dev
export PSL1GHT="$PS3DEV"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH"
make clean all
```

Salidas:

- `dualsense_fix.prx`: PRX procesado, útil para inspección.
- `dualsense_fix.sprx`: plugin firmado para CFW/HEN.
- `build/dualsense_fix.map`: mapa de enlace para depuración.

## GitHub Actions

`.github/workflows/build.yml` descarga el release oficial precompilado y fijado
de `ps3dev/ps3dev`, compila, verifica tipo, punto de entrada y secciones
esenciales del PRX, calcula SHA-256 y publica todos los artefactos. No depende
de una imagen Docker antigua ni de un comando `prxgen` inexistente.

## Instalación de prueba

1. Descarga `dualsense_fix.sprx` del artefacto de GitHub Actions.
2. Copia el archivo a una ruta de plugins de la consola, por ejemplo
   `/dev_hdd0/plugins/dualsense_fix.sprx`.
3. Añádelo al mecanismo de carga de plugins de tu CFW/HEN (por ejemplo
   `boot_plugins.txt`) y reinicia VSH.
4. Conecta solamente un DualSense estándar por USB; no conectes un DualSense
   Edge ni actives PS3XPAD para el mismo VID/PID.
5. Confirma las notificaciones “DualSense USB detectado”, “DualSense listo:
   mando y boton PS” y, cuando haya OUT, “DualSense: luz azul configurada”.
6. Prueba sticks, cruceta, botones, gatillos y PS antes de abrir un juego. No
   pruebes Bluetooth, audio, micrófono, parlante, vibración, touchpad ni
   sensores en esta versión.
7. Si falla VSH o no responde, inicia sin cargar plugins, comenta o elimina la
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
  USB asíncronas y resolución de la notificación VSH.
- El informe de entrada es `0x01` de 64 bytes; PS/Home usa `report[10]` bit 0
  y se inserta en `padData.button[0]` como `0x0001`.
- El informe USB de salida `0x02` mide 63 bytes y se usa solamente para la
  secuencia asíncrona de preparación y color azul de la barra luminosa.

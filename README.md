# DualSense Fix para PS3

Plugin VSH experimental para CFW/HEN que reclama un DualSense conectado por USB
(Sony `054c:0ce6`), crea un pad LDD de PS3 e inserta el botón PS que el soporte
genérico de PS3 omite.

## Estado actual

- Objetivo del MVP: DualSense por **USB** y botón **PS**.
- Traducción incluida: sticks, cruceta, botones frontales, L1/R1, L2/R2,
  Create/Options, L3/R3 y PS.
- Bluetooth: pendiente; el LDD USB no recibe los informes del dispositivo BT.
- Vibración, LED, touchpad, micrófono y sensores: pendientes.
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

Las notificaciones cubren carga, detección, configuración correcta,
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
4. Conecta el DualSense por USB y revisa la notificación de “mando listo”.
5. Si falla, retira el plugin en modo seguro y copia
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
- La vibración queda separada porque requiere recibir la orden de actuadores
  del pad virtual y emitir el informe de salida DualSense `0x02` por USB.

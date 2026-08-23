# DualSense Fix para PS3

Plugin VSH experimental para CFW/HEN que reclama un DualSense estándar conectado
por USB (`054c:0ce6`), crea un pad LDD de PS3 e inserta el botón PS que el
soporte genérico de PS3 omite.

## Estado actual

- Objetivo del MVP: DualSense estándar por USB, incluida la tecla PS.
- Traducción: sticks, cruceta, botones frontales, L1/R1, L2/R2,
  Create/Options, L3/R3 y PS.
- Descubrimiento dinámico de la interfaz HID y sus endpoints interrupt IN/OUT.
- Barra luminosa azul opcional; un fallo de salida no impide usar el mando.
- Bluetooth, vibración, audio, micrófono, parlante, touchpad y sensores siguen
  pendientes.
- La compilación con el SDK oficial está validada localmente. La ejecución en
  una PS3 física continúa pendiente.

## Diagnóstico

El plugin escribe estados y errores en:

`/dev_hdd0/tmp/dualsense_fix.log`

También intenta mostrar notificaciones del XMB mediante la resolución de la
exportación `vshtask` empleada por PS3XPAD/webMAN MOD. Si la exportación no está
disponible, el plugin continúa sin notificaciones y registra el problema.

## Compilación local con el SDK oficial de PS3

Requisitos:

- PlayStation 3 SDK oficial instalado y autorizado.
- Toolchain PPU GCC de `host-win32`.
- GNU Make ejecutándose en MSYS2.

El Makefile usa `C:/usr/local/cell` por defecto. Desde una consola MSYS2:

```sh
make CELL_SDK=C:/usr/local/cell -B all
```

Salidas:

- `dualsense_fix.sym`: PRX con símbolos generado por el enlazador oficial.
- `dualsense_fix.prx`: PRX oficial.
- `dualsense_fix.sprx`: imagen creada por `make_fself`.
- `objs/`: objetos y dependencias intermedias.

La compilación usa `-Wall -Wextra -Werror`, las reglas oficiales
`sdk.makedef.mk`/`sdk.target.mk` y las librerías stub de `cellUsbd`, `cellPad`,
`cellSysmodule` y `cellFs`.

## GitHub Actions

El SDK no se guarda en este repositorio ni dentro de secretos de Actions. El
workflow de Windows descarga un ZIP desde un release privado autorizado,
comprueba su SHA-256, compila y elimina el SDK del runner antes de terminar.

La preparación del repositorio privado, el archivo y los secretos se describe
en [docs/PS3_SDK_GITHUB_ACTIONS.md](docs/PS3_SDK_GITHUB_ACTIONS.md).

No se ejecuta CI en `pull_request`, porque una compilación que accede al SDK
privado no debe aceptar código no confiable.

## Instalación de prueba

1. Copia `dualsense_fix.sprx` a una ruta de plugins, por ejemplo
   `/dev_hdd0/plugins/dualsense_fix.sprx`.
2. Añádelo al mecanismo de carga de plugins de CFW/HEN y reinicia VSH.
3. Conecta solamente un DualSense estándar por USB.
4. No cargues PS3XPAD simultáneamente para el mismo VID/PID: ambos drivers
   intentarían reclamar el mismo dispositivo USB.
5. Confirma las notificaciones de detección, primera inserción LDD y, si existe
   endpoint OUT, configuración de la luz azul.
6. Si VSH falla, inicia sin plugins y conserva
   `/dev_hdd0/tmp/dualsense_fix.log` para el diagnóstico.

## Base técnica

- SDK oficial: `cellUsbd*`, `cellPadLdd*`, `cellSysmodule*`, `cellFs*`,
  `SYS_MODULE_INFO`, `SYS_MODULE_START` y `SYS_MODULE_STOP`.
- DualSense USB: informe de entrada `0x01` de 64 bytes; PS/Home está en
  `report[10]` bit 0 y se inserta como `CELL_PAD_CTRL_LDD_PS`.
- Informe de salida USB `0x02` de 63 bytes para la secuencia opcional de la
  barra luminosa azul.
- PS3XPAD se usa como referencia para el patrón LDD virtual, la inserción en
  juegos y la notificación VSH; no es una dependencia de compilación.

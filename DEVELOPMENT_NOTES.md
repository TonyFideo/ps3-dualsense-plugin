# Notas de Desarrollo: DualSense PS3 Plugin

Este documento contiene un registro detallado de todo el progreso, decisiones de diseño, obstáculos técnicos y soluciones encontrados durante el desarrollo del plugin de PS3 para soporte del DualSense (por cable).

## 1. Arquitectura y Estrategia Principal

El objetivo del plugin es hacer que un control DualSense funcione de manera nativa en la PS3 (incluyendo botón PS y vibración) sin necesidad de parchear juegos individualmente.

**Decisiones de Diseño:**
*   **Virtual Pad (LDD):** En lugar de inyectar inputs juego por juego, utilizamos el sistema LDD (Logical Device Driver) del kernel de PS3 (`lv2`).
*   **Syscalls:** Hacemos uso de los syscalls no documentados `573` (`sys_pad_dbg_ldd_set_data_insert_mode`) y `574` (`sys_pad_dbg_ldd_register_controller`). Esto le dice a la consola que asigne un mando virtual. A los ojos de la consola y los juegos, es un DualShock 3 oficial.
*   **Intercepción USB:** Nos conectamos directamente al controlador USB de la PS3 (`SYSMODULE_USBD`) para leer los reportes crudos del DualSense, los procesamos (para mapear botones) y los enviamos al pad virtual. La vibración toma la ruta inversa: capturamos la solicitud del sistema y le enviamos un reporte USB crudo al mando.

## 2. Lo que se ha programado hasta ahora

*   **Infraestructura del Repositorio:**
    *   `Makefile`: Configurado para compilar un `.sprx` usando el SDK PSL1GHT.
    *   `.github/workflows/build.yml`: Acción de CI configurada para usar contenedores Docker y compilar automáticamente (actualmente con obstáculos, ver sección de problemas).
*   **Código Fuente (`source/` e `include/`):**
    *   `main.c`: Entry point del módulo VSH. Carga el `SYSMODULE_USBD` e invoca la inicialización de nuestro código.
    *   `dualsense_usb.h`: Contiene las definiciones estructurales del estándar USB (`UsbDeviceDescriptor`, `UsbEndpointDescriptor`, etc.) y las firmas del driver USB de la PS3 (`CellUsbdLddOps`).
    *   `dualsense_usb.c`:
        *   Implementa los *wrappers* para invocar los syscalls 573 y 574 en PSL1GHT (`lv2syscall4`).
        *   Implementa las rutinas del driver USB: `probe`, `attach`, `detach`.
        *   Filtra dispositivos USB: solo reacciona si el dispositivo conectado tiene el **VID 0x054C** y el **PID 0x0CE6** (DualSense).
        *   Si detecta el DualSense, inyecta con éxito el mando virtual en el sistema.

## 3. Detalles Técnicos Clave (Mapeo)

A través de volcados (dumps) del DualSense conectado por USB, descubrimos lo siguiente:
*   **Input Report (ID 0x01):** El botón "PS" (PlayStation) se encuentra mapeado exactamente en el **Byte 10, Bit 0** del paquete de datos de entrada crudo.
*   **Output Report (ID 0x02):** La vibración (haptic feedback) utiliza un formato propietario diferente al del DS3. Los datos de los motores de vibración deben escribirse usando `cellUsbdInterruptTransfer` al *endpoint* de salida correspondiente.

## 4. Obstáculos Encontrados y Soluciones

### Obstáculo 1: Ausencia de Cabeceras Oficiales (`<cell/usbd.h>`)
**El Problema:** El código abierto SDK de `PSL1GHT` no incluye los headers oficiales de Sony para el manejo de USB de bajo nivel, los cuales son necesarios para interactuar con los periféricos.
**La Solución:** Investigamos implementaciones *open-source* (como `ps3steampad` y `PS3xPAD`). Extrajimos las definiciones en crudo de las estructuras USB y las empaquetamos manualmente en nuestro `dualsense_usb.h`. Esto mantiene nuestro proyecto 100% legal y libre de código filtrado del SDK de Sony. Para las funciones (como `cellUsbdOpenPipe`), hemos decidido enlazarlas vía stubs (ej. `-lusbd_stub`) o resolviendo sus NIDs (Name Identifiers) en tiempo de ejecución.

### Obstáculo 2: Contenedores Docker para GitHub Actions
**El Problema:** Para facilitar la compilación, configuramos GitHub Actions. Sin embargo, los contenedores estándar de la comunidad (`ps3dev/ps3dev:latest`, `mlafeldt/ps3dev:latest`, etc.) están fallando al ser descargados (arrojando "pull access denied"). Al parecer, estos repositorios en Docker Hub fueron eliminados, hechos privados, o su ubicación cambió recientemente.
**Estado Actual / Duda:** Esto nos deja momentáneamente sin un entorno automatizado para confirmar que el código compila bien.
*   *Posible solución A:* Proveer un nombre de imagen de Docker válido si se conoce alguno activo.
*   *Posible solución B:* Dejar de usar Actions y utilizar MSYS2 localmente. Notamos que la carpeta `C:\PSL1GHT` local de la máquina tiene el código fuente del SDK, pero no los binarios ya compilados (`ppu_rules` faltante).

## 5. Próximos Pasos Pendientes

1.  **Resolver la Compilación:** Necesitamos un entorno `psl1ght` funcional (ya sea un Docker arreglado o un entorno local de MSYS2 listo) para poder generar el archivo `.sprx` o un entorno docker que funcione para el workflow de github actions.
2.  **Lectura (Thread Input):** Leer constantemente el Endpoint In del USB, atrapar el `Byte 10`, parsearlo al formato clásico del DualShock 3 (CellPadData) e introducirlo al pad virtual.
3.  **Vibración (Output):** Atrapar el *rumble* desde `sys_pad`, empaquetarlo en un Reporte 0x02, y enviarlo por el Endpoint Out del DualSense para que vibre correctamente.

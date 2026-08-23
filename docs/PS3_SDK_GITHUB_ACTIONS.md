# SDK oficial de PS3 en GitHub Actions

El SDK oficial no debe añadirse al repositorio del plugin ni publicarse como un
artefacto de Actions. La instalación local se identifica como material
confidencial; usa este procedimiento solamente si tu licencia permite alojar y
ejecutar el SDK en infraestructura de GitHub.

## Diseño

El ZIP se guarda como asset de un release en un repositorio privado separado.
El workflow obtiene acceso mediante un token de solo lectura, comprueba el
SHA-256 y extrae el SDK fuera del workspace. Los artefactos publicados contienen
solo el PRX, el SPRX y los informes de inspección.

GitHub limita cada secreto a 48 KB, por lo que el SDK no cabe ni debe codificarse
directamente en un secreto. Los secretos contienen únicamente credenciales y
metadatos de descarga.

## 1. Crear el archivo

El ZIP debe contener una carpeta superior llamada `cell`:

```text
ps3sdk-475.001-win32.zip
└── cell/
    ├── host-win32/
    ├── samples/
    └── target/
```

Ejemplo con `tar.exe`, incluido en Windows, desde PowerShell:

```powershell
New-Item -ItemType Directory -Force C:\temp | Out-Null
C:\Windows\System32\tar.exe -a -c -f C:\temp\ps3sdk-475.001-win32.zip -C C:\usr\local cell
Get-FileHash C:\temp\ps3sdk-475.001-win32.zip -Algorithm SHA256
if ((Get-Item C:\temp\ps3sdk-475.001-win32.zip).Length -ge 2GB) {
  throw "El asset supera el limite de 2 GiB de GitHub Releases"
}
```

No ejecutes este comando dentro del repositorio del plugin.

## 2. Guardarlo en un repositorio privado

Crea un repositorio privado independiente, por ejemplo
`OWNER/ps3-sdk-private`, y un release privado:

```powershell
gh release create ps3-sdk-475.001 `
  C:\temp\ps3sdk-475.001-win32.zip `
  --repo OWNER/ps3-sdk-private `
  --title "PS3 SDK 475.001 authorized archive" `
  --notes "Private authorized toolchain. Do not redistribute."
```

Antes de subirlo, confirma que el repositorio sea realmente privado y que tu
licencia permita este almacenamiento. No uses el repositorio público del
plugin, Git LFS, Actions artifacts ni Actions cache para guardar el SDK.

## 3. Crear acceso de solo lectura

Crea un fine-grained personal access token limitado al repositorio privado del
SDK con únicamente `Contents: Read`.

En el repositorio del plugin crea un environment llamado `ps3-sdk`, con
aprobación manual si tu plan lo permite, y añade estos secretos:

| Secreto | Valor |
| --- | --- |
| `PS3_SDK_TOKEN` | Token de solo lectura del repositorio privado |
| `PS3_SDK_REPOSITORY` | `OWNER/ps3-sdk-private` |
| `PS3_SDK_TAG` | `ps3-sdk-475.001` |
| `PS3_SDK_ASSET` | `ps3sdk-475.001-win32.zip` |
| `PS3_SDK_SHA256` | SHA-256 completo del ZIP |

## 4. Ejecutar

Abre **Actions  Build DualSense Fix (Official PS3 SDK)  Run workflow**.
También se ejecutará en pushes a `master` y `testeo/**` cuando los secretos del
environment estén disponibles.

El workflow deliberadamente no escucha `pull_request`. Solo personas de
confianza deben poder modificar o ejecutar código que reciba acceso al token y
al SDK privado.

## Alternativa recomendada

Si la licencia no permite alojar el SDK en GitHub, usa exactamente el mismo
repositorio con un runner Windows propio que ya tenga el SDK instalado. Esa
modalidad evita transferir el SDK a infraestructura ajena.

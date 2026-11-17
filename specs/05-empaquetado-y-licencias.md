# 05 · Empaquetado y licencias

Requisito heredado: **build autocontenido**, sin dependencias externas en runtime (`specs/heredadas/00`, principios). Nada de instalar Qt, mpv, Node ni ffmpeg.

## 1. Windows (x64)

- Compilador: MSVC (Visual Studio 2022 Build Tools) con Qt 6 para MSVC 2022 64-bit.
- `windeployqt` copia las DLL de Qt y los plugins necesarios (platforms, imageformats, qml). Revisar que no arrastre módulos no usados.
- **libmpv**: `libmpv-2.dll` (o `mpv-2.dll`) de una compilación para Windows con soporte de libass, ffmpeg y los *hwdec* (D3D11VA/DXVA2). Se vendoriza en `third_party/` con su checksum; el script `fetch:libmpv` la descarga y verifica.
- Distribución: **ZIP portable** (`CinemaLove-Qt-Windows-x64-<versión>.zip`), igual que la versión Electron. Opcional más adelante: instalador.
- El sidecar (opción A): `backend/node.exe`, `backend/main.js`, `better-sqlite3` precompilado.
- Si Node/MSVC requieren runtime de Visual C++, incluir `vcruntime`/`msvcp` junto al ejecutable (despliegue local de la app).

## 2. Linux (x64)

- Build en una distro base antigua (p. ej. Ubuntu 22.04 en contenedor) para compatibilidad de glibc.
- **AppImage** con `linuxdeploy` + `linuxdeploy-plugin-qt` (Qt, plugins y QML) y libmpv incluida en el AppDir con sus dependencias (libass, ffmpeg, libplacebo si procede). **tar.gz** como alternativa.
- Probar en Ubuntu y Fedora, en **X11 y Wayland** (Qt usa `xcb` o `wayland`; comprobar el contexto OpenGL y los *hwdec* VAAPI/NVDEC en ambos).
- Sidecar: `backend/node` + `main.js` + `better-sqlite3` precompilado (musl/glibc según el build).

## 3. Compilación de libmpv

Preferir una compilación **LGPL** de libmpv (ver §4). Para Windows existen builds precompilados de la comunidad; para Linux, compilar con Meson sin los componentes GPL o usar la de la distro en el contenedor de build. **Verificar la licencia concreta del binario elegido antes de distribuir** (algunos builds listos para usar son GPL).

## 4. Licencias

| Componente | Licencia | Implicación |
|---|---|---|
| Qt 6 (módulos usados) | LGPLv3 / comercial | Enlace **dinámico** (DLL/so de Qt sin modificar), aviso de licencia en «Acerca de» y poder sustituir Qt por el usuario. Sin módulos GPL-only |
| libmpv | LGPLv2.1+ (si se compila sin GPL) o GPLv2+ | Con un build **GPL**, toda la aplicación pasaría a obligaciones GPL al distribuirse. Usar build LGPL |
| ffmpeg (dentro de mpv y del sidecar) | LGPL/GPL según configuración | Igual: build LGPL sin `--enable-gpl` ni `--enable-nonfree` |
| Node.js | MIT | Incluir aviso |
| Material Symbols / fuentes | Apache 2.0 / OFL | Incluir avisos |

- La pantalla «Acerca de» (`specs/heredadas/20` Fase 7) lista todas las licencias y las versiones de Qt, libmpv, ffmpeg y Node.
- No es asesoría legal: validar el cumplimiento antes de una distribución pública.

## 5. Scripts previstos

| Script | Qué hace |
|---|---|
| `fetch:libmpv` | Descarga libmpv por plataforma con checksum |
| `build:backend` | Empaqueta el sidecar (esbuild + node + better-sqlite3) |
| `configure` / `build` | `cmake --preset …` / `cmake --build` |
| `test` | `ctest` (Qt Test + qmltestrunner) |
| `dist:win` | `windeployqt` + ZIP |
| `dist:linux` | AppImage + tar.gz |

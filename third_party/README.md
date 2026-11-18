# Binarios de terceros incluidos

El proyecto no descarga nada para compilarse ni ejecutarse: lo que necesita está en esta carpeta.
Los binarios grandes **no se versionan en git** (límite de 100 MB por archivo) pero **forman parte de la carpeta**: copiar `qt6-version/` completa los incluye.

| Componente | Ruta | Licencia | SHA-256 |
|---|---|---|---|
| libmpv 0.42 (mpv v0.42.0-93-g3d73d07e6), build **LGPL** (`-Dgpl=false`, FFmpeg sin `--enable-gpl`/`nonfree`) | `libmpv/libmpv-2.dll`, `libmpv/include/` | LGPL v2.1+ | `480ad9d89a22f037f24b814aeeb6d350ae827238d68cb2f114d4550c1c52ea9d` |

Procedencia (solo informativa): libmpv de la compilación comunitaria «mpv-winbuild» del 15/11/2026 (variante `mpv-dev-lgpl`); el aviso de libmpv/FFmpeg se muestra en «Acerca de».

El backend (API, escáner, TMDb, servidor público con HTTPS, mDNS y QR) es código C++ propio en `../src/server/` y `../src/client/`: no hay Node ni dependencias de JavaScript.
Qt 6, MSVC y CMake son la cadena de herramientas del equipo de desarrollo, no dependencias del proyecto.

## Tipografías (third_party/fonts/)
Licencia SIL Open Font License 1.1 (textos en `OFL-Fraunces.txt` y `OFL-Inter.txt`); se cargan al arrancar si existen.

| Archivo | SHA-256 |
|---|---|
| `Fraunces-VariableFont_SOFT,WONK,opsz,wght.ttf` (títulos) | `99b365ece2cccd7d7baf879cc141f1fa9f2d20690fe025bc3003b01dff098f05` |
| `Inter-VariableFont_opsz,wght.ttf` (interfaz) | `0be2399ea925f1f83ff974764761da9860ec50742ed29a5d4c1ffd0c5c7ac3a8` |

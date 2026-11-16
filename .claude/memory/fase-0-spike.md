---
name: fase-0-spike
description: Estado del spike de la Fase 0 (Qt 6 + libmpv): cómo compilar/ejecutar, resultados medidos, problemas resueltos y pendientes
metadata:
  type: project
---

# Fase 0 — spike (15/11/2026)

**Resultado hasta ahora:** mpv dentro de Qt Quick funciona en Windows: video decodificado por mpv en un FBO OpenGL con los controles QML dibujados **encima** (sin ventanas hijas). Verificado con captura (`grabWindowTo`) y autoprueba. NO está validada la puerta de decisión completa (ver «Falta»).

## Entorno de desarrollo (esta máquina)
- Qt 6.8.3 LTS (msvc2022_64) en `D:\Qt` (instalado con aqtinstall + Python 3.12 por winget). `QT_ROOT=D:/Qt/6.8.3/msvc2022_64`.
- MSVC 2022 Build Tools; CMake y Ninja de Visual Studio (no están en PATH): `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
- libmpv: `scripts/fetch-libmpv.ps1` (build shinchiro 20261111, **GPL**: solo para desarrollo; para distribuir hace falta build LGPL, ver specs/05).
- GPU del equipo: AMD Vega 8 (iGPU) + NVIDIA GTX 1650; Qt eligió la Vega 8 (OpenGL 3.3 core).

## Compilar y ejecutar
```
$env:QT_ROOT="D:/Qt/6.8.3/msvc2022_64"
cmake --preset win ; cmake --build --preset win-release
build\win\Release\cinemalove.exe <archivo> [--selftest --stats-out ruta.json]
```
`--selftest` hace seek, cambio de audio/subtítulo, pausa, volumen, captura PNG y vuelca métricas JSON.
Genera `mpv.lib` (MSVC) a partir de la DLL (cmake/Libmpv.cmake). Los logs van a `cinemalove-spike.log` junto al exe.

## Resultados (muestras sintéticas 1080p/720p, ver scratchpad de la sesión)
- H.264 + AC3 5.1 en MKV: `d3d11va-copy`, 24 fps, 0 descartados por decodificador, seek/audio/subtítulo OK.
- HEVC 10-bit (p010) + E-AC3 en MKV: `d3d11va-copy`, OK.
- AVI MPEG-4: software, OK.
- Sin sesiones ffmpeg ni 416: reproduce el archivo directamente.

## Problemas resueltos (no repetir)
- **Crash al cerrar en `qwindows.dll`** (0xC0000005) tras `mpv_terminate_destroy` en el hilo de la GUI. Solución: liberar el `mpv_render_context` en el hilo de render (`scheduleRenderJob`/`sceneGraphInvalidated`) y llamar a `mpv_terminate_destroy` **desde un `std::thread` aparte**. Hipótesis (no confirmada): mpv desinicializa estado por hilo (COM) en el hilo que lo destruye.
- Si el contexto de render sigue vivo, `mpv_terminate_destroy` aborta el proceso (0xC0000409): se omite.
- `shutdown()` debe llamarse con el bucle de eventos vivo (desde QML `onClosing`/antes de `Qt.quit`), no desde `aboutToQuit`.
- Las cabeceras de tipos QML necesitan `target_include_directories(... src/player)`.
- Forzar OpenGL: `QQuickWindow::setGraphicsApi(OpenGL)` antes de crear la app.

## Falta para cerrar la Fase 0 (specs/06)
- Prueba larga: película real ≥ 2 h con pausas y saltos, y app abierta horas (ventana en blanco de la versión Flutter).
- Archivos reales de la biblioteca: PGS, archivo en red (SMB) y por HTTP con `Range`.
- Linux (X11 y Wayland) y plan B (Qt Multimedia) si hiciera falta.
- Probar ventana sin marco (el spike usa marco normal) y redimensionado/pantalla completa con controles.
- Decidir interfaz QML vs web embebida y backend sidecar vs C++ (specs/00 §4).

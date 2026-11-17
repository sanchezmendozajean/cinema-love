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
- libmpv: `scripts/fetch-libmpv.ps1` (build **LGPL** de zhongfly/mpv-winbuild 2026-11-15, mpv v0.42). Verificado con `mpv-configuration` (`-Dgpl=false`) y `ffmpeg-configuration` (sin `--enable-gpl`/`nonfree`/`version3`). El build de shinchiro (`mpv-dev-x86_64`) es GPL y no tiene variante LGPL: no usarlo para distribuir.
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

## Ventana sin marco (16/11/2026)
Hecho y verificado con capturas (`--selftest` genera `*-fs.png` y `*-small.png`): `Qt.FramelessWindowHint`, barra de título propia (arrastre con `startSystemMove`, min/max/cerrar), bordes con `startSystemResize`, pantalla completa y redimensionado a 900x520 con controles superpuestos y video correcto. Pendiente visual: el subtítulo ASS se solapa con la barra de controles (mpv `sub-margin-y`/posición según controles visibles).

## Falta para cerrar la Fase 0 (specs/06)
- Prueba larga: película real ≥ 2 h con pausas y saltos, y app abierta horas (ventana en blanco de la versión Flutter).
- Archivos reales de la biblioteca: PGS, archivo en red (SMB) y por HTTP con `Range`.
- Linux (X11 y Wayland) y plan B (Qt Multimedia) si hiciera falta.
- Decidir interfaz QML vs web embebida y backend sidecar vs C++ (specs/00 §4).

## Arranque de desarrollo
`npm run dev [-- args de la app]` (scripts/dev.ps1): configura si falta, compila y lanza. `npm run dev:debug`, `npm run configure`.

## Selector de películas / catálogo (16/11/2026)
Adelanto de la Fase 2 a petición del usuario (la puerta de la Fase 0 sigue sin cerrarse).
- `src/catalog/MovieModel` (QtSql, **solo lectura** `QSQLITE_OPEN_READONLY`) lee la copia `.dev-data/cinemalove.sqlite` (copiada de `flutter-version/datos/`); 537 películas con `estado_archivo='ok'`. Ruta del archivo = `libraries.ruta_actual` + `ruta_relativa` (E:\Cine y TV\Películas\…). Búsqueda sin tildes. Si `titulo` es NULL (hay 2) se usa el nombre de la carpeta.
- QML: `theme/Tokens.qml` (singleton, colores de design/10), `PosterCard`, `CatalogView` (cabecera, búsqueda, GridView, estados vacío/error), integrado en `Main.qml`: clic → `openMovie` → reproduce; «‹ Biblioteca»/Esc/Retroceso vuelven; fin de archivo vuelve al catálogo; aviso si el archivo no está accesible.
- Pósters: URL de TMDb cargadas con caché de disco (`.dev-data/img-cache`, `CachedNam` en main.cpp). Solo desarrollo: casi ninguno está en `images/` (cl_image_cache). El `ImageProvider` definitivo sigue pendiente.
- Pruebas: `--catalog-shot ruta.png [--search txt] [--open-first]`, `--db ruta`.
- Pendiente: fuentes Fraunces/Inter locales (hoy Georgia/sistema), progreso/«continuar viendo», ficha de película, rendimiento con scroll (criterio 60 fps), foco con teclado verificado solo en código.

## Ficha de película (16/11/2026)
`qml/DetailView.qml` (design/11 · P5, versión reducida): backdrop con degradados, póster, título/original/año/duración/idioma, chips de género, estudio, botón Reproducir / Continuar (con minutos restantes y barra de progreso, leído de `watch_progresses`) / Volver a ver, ↺ reiniciar, «Vista», sinopsis, reparto (fotos circulares con `MultiEffect` + máscara) y equipo (dirección/guion). Flujo: catálogo → ficha → reproductor; «‹ Ficha»/Esc vuelven al paso anterior. `MovieModel.get(row)` devuelve el mapa completo.
- El progreso es **solo lectura**: reproducir aquí todavía NO guarda posición (escritura aditiva en `watch_progresses` pendiente, tras acordar con la API/otras versiones).
- Pruebas: `npm run dev -- --catalog-shot x.png --search "The Majestic" --open-first [--play]`.
- Pendiente de la ficha: extras, colecciones, detalles técnicos, pistas de audio/subs recordadas (`pista_*_id`), estados sin metadatos y skeletons.

## Guardar y reanudar progreso (16/11/2026)
- `MovieModel::saveProgress` replica `escritura.ts`: upsert en `watch_progresses` con `usuario_id IS NULL`, `ultima_reproduccion` en segundos, conserva pistas si no llegan nuevas; «visto» → posición 0 + borra de `proximas_a_ver`. **Solo escribe si la ruta contiene `/.dev-data/`** (si no, avisa y omite).
- Eventos de guardado (specs/05 §4): pausa, seek, cambio de pista (agrupados con un Timer de 700 ms), cerrar el reproductor/ventana, y >95 % o fin de archivo → visto. Nunca periódico. No se guarda con posición < 5 s ni antes de restaurar pistas (`progressReady`), para no pisar las guardadas con «auto».
- Reanudar: `load(ruta, start)` + reselección única de audio/subtítulo al conocer las pistas. Verificado: guarda 311 → reanuda en 318,7 → guarda 622; audio `1`, sub `no`.
- Catálogo: barra de progreso y marca ✓ en las tarjetas; la ficha se refresca al volver.
- Pendiente: mapear subtítulos externos `ext-k` ↔ ids de mpv (specs/05 §3), ids de pista con las otras versiones.
- **Trampa de PowerShell**: `-db` es alias de `-Debug`; por eso la opción de la app es `--database` (no `--db`). Los argumentos con espacios pasan bien por `npm run dev --`.
- Las pruebas que escriben deben usar `--database` con una copia aparte en `.dev-data/` (la base original de `flutter-version/datos/` nunca se toca). Prueba: `--progress-test --search "101 d"` (ejecutar dos veces).

## Inicio (16/11/2026)
`HomeView` (design/11 · P2, RF-11): héroe con backdrop (lo último en curso; si no hay, lo más reciente) + filas «Continuar viendo» (tarjetas apaisadas `LandscapeCard`, minutos restantes), «Próximas a ver» (`proximas_a_ver`), «Agregados recientemente» (`creado_en`), «Vistos recientemente»; filas vacías se ocultan. Datos: `MovieModel::homeRow(kind, limit)` (continue|queue|recent|watched) + señal `homeChanged` al guardar progreso. Pestañas Inicio/Películas en la barra de título (`root.section`); Inicio es la sección por defecto.
- Trampa QML: no usar `readonly property var x: { rev; return model.f() }` — no se reevaluaba al llegar el modelo; se usa `refresh()` explícito. Y no nombrar una propiedad igual que un `id` del mismo archivo.
- Pruebas: `--section home|catalog`, `--home-scroll px`, `--database` con copia aparte.
- Pendiente: series/episodios en «Continuar viendo», flechas ‹ › en las filas, skeletons de carga, ítem con foco por teclado en filas, filas por género (no están en RF-11).

## Series y temporadas (16/11/2026)
- `SeriesCatalog` (QAbstractListModel con los mismos roles que `MovieModel`, así `CatalogView` sirve para ambos): series → temporadas (derivadas de los episodios con `estado_archivo='ok'`) → episodios (`media_items.series_id`). 19 series, ~1.100 episodios; 499 sin título → «Episodio N». Pósters de temporada de `seasons.poster_url`; still del episodio = `media_items.poster_url` (w300).
- «Episodio sugerido» (`detail().next`): en curso más reciente → siguiente al último visto → primero → «volver a ver». El botón de la ficha lo refleja («Continuar T3 · E1 — …», «Siguiente: …»).
- `ProgressWriter` (src/catalog) es el único sitio que escribe en `watch_progresses` (película y episodio); solo copias de `.dev-data/`.
- UI: sección Series, `SeriesDetailView` (héroe, temporadas con ✓/progreso, lista de episodios con still, «Siguiente», sinopsis, reparto, equipo). Reproducir un episodio guarda/reanuda igual que una película; al terminar salta al siguiente episodio sin volver a la ficha (`nextEpisodeId`).
- Inicio: «Continuar viendo» mezcla películas y episodios (tarjeta = serie + «T1 · E3»).
- Verificado: reanudar T3·E1 de Banshee en 1525 s (HEVC, d3d11va-copy), guardado a 1535 s conservando subtítulo. NO verificado: salto automático al siguiente episodio, «Marcar temporada como vista», disponibilidad de archivo por episodio.
- Pendiente: «Marcar temporada/serie como vista» (escritura masiva, pedir confirmación), selector de temporada ▾, series en «Agregados recientemente», fuentes locales, ficha de persona, colecciones.
- Pruebas: `--section series --search banshee --open-first [--play] --home-scroll 640 --catalog-shot x.png --database <copia>`.

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

## Backend sidecar y Administración (16/11/2026) — opción A de specs/03
- `backend/`: el backend de Electron (`src/main` sin `index.ts` + `src/shared`) copiado por `npm run sync` (`backend/scripts/sync-from-electron.mjs`, NO modifica electron-version) a `backend/src/vendor/`; `src/electron-shim.ts` sustituye `electron` (alias de esbuild) y `src/headless.ts` arranca `AppCore` sin ventana (protocolo por stdout: `CL_READY {baseUrl,token,datos}` / `CL_ERROR`; se cierra con SIGTERM, stdin cerrado u orden `quit`). `npm run build` → `backend/dist/main.cjs` (esbuild; `better-sqlite3` queda externo en `node_modules`). Vite `?raw` se resuelve con un plugin.
- Requiere `npm install` en `backend/` (better-sqlite3 se baja precompilado). En dev usa el `node` del PATH; empaquetado irá `backend/node.exe`.
- `BackendLauncher` (C++): variables `CL_DATA_DIR`, `CL_APP_ROOT`, `CL_SIN_FFMPEG=1`; un reintento si muere; `stop()` en `aboutToQuit`. **Solo arranca sobre carpetas con `.dev-data` y con archivo `cinemalove.sqlite`** (el backend siempre abre ese nombre). Arranque perezoso al abrir Administración.
- `ApiClient` (C++): `request(method, path, body, cb(status, datos, error))` con `X-Api-Key`.
- UI: `AdminView` (Bibliotecas: alta con selector de carpeta, escanear, metadata faltantes/todo, baja con confirmación escribiendo el nombre; Tareas con progreso y cancelar, sondeo cada 1,2 s; TMDb: clave, probar, guardar), `Modal` (X visible, sin cierre al hacer clic afuera), `AdminButton`. Al terminar un trabajo se recargan `MovieModel`/`SeriesCatalog` (`reload()`).
- Verificado: backend arranca con Node plano, `/health` y `/admin/bibliotecas` responden, «Escanear todo» sobre la biblioteca real (E:\) completó desde la UI sobre una copia, sin procesos huérfanos al cerrar. Dos escritores (Qt: progreso; backend: escáner) comparten la base con busy_timeout.
- Pendiente de Administración: huérfanos, re-vincular, homologar nombres, edición manual y asistente de TMDb para ambiguos/sin coincidencia, usuarios/servidor público/sesiones/QR (Fase 5), ajustes de datos (limpiar caché).
- Cuidado: un `node.exe` de Adobe corre en esta máquina; no es del proyecto.

## Imágenes (descargas) vía backend (16/11/2026)
- `ClImageProvider` (`image://cl/<url TMDb codificada>`, `Images.src(url)` en QML): pide al backend `/api/v1/image?src=<url exacta>` (índice de Flutter → `cl_image_cache` → descarga de TMDb a `images/cl/`, **la caché es compartida con las otras versiones**). Decodifica reducido según `sourceSize` en el pool de hilos. Espera si el backend está «iniciando»; si falla/está detenido descarga directo de TMDb.
- Trampa: `requestImageResponse` llega desde OTRO hilo; la respuesta se mueve al hilo de la red (`moveToThread` + `start()` en cola) o Qt avisa «Cannot create children for a parent that is in a different thread».
- `BackendLauncher` ahora lo crea/inicia `main.cpp` (propiedad de contexto `backend`) y debe declararse ANTES que el `QQmlApplicationEngine` (si no, warnings de bindings con null al cerrar). `--no-backend` lo omite.
- El ensayo escribió `D:\x.png` (raíz del disco D:) por una captura de prueba mal dirigida; el usuario debe borrarlo (el borrado lo bloqueó la comprobación de seguridad).

## Menú ⋯, Colecciones y Próximas a ver (16/11/2026)
- Todas las acciones de usuario van por la API del backend con el token admin (`usuarioId` null, igual que el modo local de Electron): `/api/v1/watchlist/:id` (PUT/DELETE), `/media/:id/progress` (PUT estado visto/noVisto), `/collections*`, `/series/:id/progress` (temporada o serie entera), `/media/:id/user-state`. Tras cada mutación se recargan los modelos (`refreshAfterMutation`).
- `ItemMenu` (Popup no modal), botón ⋯ en `PosterCard`/`LandscapeCard` (hover/foco/tecla Menú), `CollectionsView` (lista con mosaico 2×2 + «Próximas a ver» fija; detalle con grilla; crear/renombrar/eliminar con `Modal`). Marcar «no vista»/serie/temporada siempre pide confirmación (borra posiciones de reanudación).
- `SeriesDetailView`: botones «Marcar temporada/serie como vista/no vista».
- Trampa QML: en un `Repeater` anidado `modelData` del interior tapa al exterior → capturar en una propiedad (`readonly property var col: modelData`).
- Pruebas: `--ui-test collection|special|menu|markconfirm`.

## Administración: pendientes de metadata y huérfanos (16/11/2026)
- Tras un trabajo de metadata, «Revisar N pendientes» abre la lista (`ambiguos` + `sinCoincidencia`) → «Elegir…» busca en TMDb (`/admin/tmdb/buscar`, usa la clave guardada en la base) → «Usar esta» (`POST /admin/{items|series}/:id/tmdb`). Verificado con una búsqueda real.
- «N sin archivo ›» en la biblioteca abre la lista de huérfanos; «Quitar N del catálogo» pide una segunda confirmación explícita (borra progreso/metadata) y el backend exige `confirm=<cantidad>`. En los datos reales hay 4 películas Godzilla y 35 episodios sin archivo (disco/carpeta movidos): NO se tocaron.
- Pruebas: `--ui-test orphans|tmdb`.

## Servidor público, ventana, «Acerca de» (16/11/2026)
- `ServerPanel` (pestaña «Servidor» de Administración): estado/URLs/QR (SVG vía `/admin/servidor/qr`), configuración (nombre, HTTP/HTTPS, puerto, mDNS, autostart), usuarios (crear/cambiar contraseña/eliminar escribiendo el nombre), token de acceso (mostrar/copiar/regenerar con confirmación), regenerar certificado con aviso. Verificado solo con el servidor DETENIDO (no se probó iniciarlo/mDNS/QR por no abrir la LAN en pruebas).
- `Check.qml`: casilla con estilo oscuro (el Basic por defecto es blanco).
- Instancia única (QLocalServer `cinemalove-qt6-instancia`; la segunda ejecución trae al frente la primera y sale; `--multi` la desactiva; los modos de prueba `--selftest/--catalog-shot` no la usan). Ventana persistente en `%LOCALAPPDATA%\CinemaLove\window.ini` (se guarda al salir, se descarta si queda fuera de pantalla). Cuidado: `QSettings` capturada por referencia en `aboutToQuit` era un bug (vida del objeto).
- `AboutDialog` (ⓘ en la barra de título): versiones reales de libmpv/FFmpeg y licencias (Qt LGPL, libmpv LGPL, FFmpeg LGPL, Node MIT, TMDb).
- Bug corregido: el id de `image://cl/…` llega CODIFICADO; hay que decodificar si no contiene «://» (antes solo funcionaba con backend por casualidad).
- Trampa de herramientas: en scripts Python embebidos, `"\n"` llega como un salto de línea real; usar `chr(92)`.

## Qué falta (resumen honesto, 16/11/2026)
Fase 1: asistente de bienvenida/carpeta de datos (hoy solo `.dev-data` por seguridad), fuentes Fraunces/Inter locales, modo gráfico de respaldo. Fase 2: ficha de persona, skeletons, foco de teclado, 60 fps con 2000 pósters sin medir. Fase 4: re-vincular, homologar nombres, edición manual de metadata. Fase 5: Modo Cliente (conectar a otro CinemaLove), descubrimiento mDNS en la UI, sesiones activas. Fase 6: empaquetado (`dist:win`), instalación en máquina limpia, Linux, logs rotatorios, prueba de ≥ 8 h. Fase 0 pendiente: prueba larga, PGS, SMB/HTTP, Linux.


## Proyecto autocontenido (16/11/2026) — decisión del usuario
**Regla:** `qt6-version/` no depende de otras carpetas (electron-version, flutter-version) ni descarga nada de internet para compilar/ejecutar; la única conexión del producto es TMDb (datos y pósters). Si necesita un backend, corre con **código propio**.
- Backend: `backend/src/{main,shared,headless.ts,platform.ts}` es código propio (ya no existe `vendor/`, ni el script de sincronización, ni el alias de `electron`; `app-core.ts` importa `../platform`). Se compila con `scripts/build-backend.ps1` (Node y esbuild incluidos). `backend/node_modules` está versionado (2.971 archivos) para no necesitar `npm install`. Los restos de «electron» en el backend son identificadores de compatibilidad (`plataforma:'electron'` en `/health`, nombre de respaldos).
- `third_party/node/node.exe` (v24.18.0) y `third_party/libmpv/libmpv-2.dll`: incluidos en la carpeta pero NO en git (>100 MB / límite de archivo); checksums y procedencia en `third_party/README.md`. `fetch-libmpv.ps1` eliminado. `BackendLauncher` usa solo ese Node (nunca el PATH).
- Specs heredadas copiadas a `specs/heredadas/` y las referencias se reescribieron.
- Verificado: backend compilado y app ejecutada con un PATH SIN Node ni npm.
- Herramientas de desarrollo externas (no son dependencias del proyecto): Qt 6.8.3 en `D:\Qt`, MSVC Build Tools, CMake.
- Ya no valen las instrucciones previas de `npm install`/`npm run sync` en `backend/`.

## Trampa de compilación (17/11/2026)
La compilación completa (CMake + qmlcachegen de ~35 archivos QML + windeployqt) tarda **> 2 minutos**. Un `timeout` ≤ 115 s la corta a medias y el `.exe` queda VIEJO sin avisar (comprobar siempre la fecha de `build/win/Release/cinemalove.exe`). Compilar con `run_in_background` y esperar al aviso; ejecutar los ejecutables de prueba por separado.


## Datos reales: asistente, respaldo y bloqueo (17/11/2026) — decisión del usuario («sí, hazlo así»)
- **Backend** (`backend/src/main/db/database.ts`): respaldo con `VACUUM INTO` → `cinemalove.sqlite.bak-qt-AAAAMMDD` si no hay ninguno o el último tiene > 7 días (conserva 5; copia de archivo si falla); bloqueo por PID en `cinemalove.electron.lock` (lo respeta Electron y detecta a Qt) y `cinemalove.qt.lock`; solo se sueltan los bloqueos propios. Flutter no escribe bloqueo (no detectable). `CL_READY.datos` incluye `respaldo`.
- **Qt**: `DataFolder` (config.yaml junto al exe si es escribible, si no en AppConfigLocation; formato Flutter `database_directory`; `inspect()` cuenta películas/series y detecta bloqueo con PID vivo; `apply`/`forget`/`backups`). `BackendLauncher` expone `errorCode` (`bloqueada`|`version-antigua`|`error`) e `info`; no reintenta tras `CL_ERROR`. Los modelos leen la base SOLO cuando `backend.state==="listo"` (el backend va primero: respaldo+bloqueo+migraciones).
- **UI**: `StartupView` (asistente: Bienvenida → Datos → Abriendo → TMDb → Biblioteca → Listo; pantalla «Abriendo…»; error «Tu colección está en uso» con Reintentar / Elegir otra carpeta), pestaña **Datos** de Administración (carpeta, respaldos, «Usar otra carpeta…»).
- **Seguridad de pruebas**: `ProgressWriter::setRequireDevCopy` + `BackendLauncher.requireDevCopy` solo en modo de pruebas; `main.cpp` sale con código 3 si una prueba apunta fuera de `.dev-data/`. `scripts/dev.ps1` añade por defecto `--database .dev-data/cinemalove.sqlite`. Caché de imágenes de Qt ahora en CacheLocation.
- Verificado (ejecutable nuevo): respaldo consistente (1.662 registros), bloqueos creados/retirados, segundo arranque rechazado, bloqueo REAL de otro proceso respetado sin tocar nada, recorrido «elegir carpeta → config.yaml → abrir con respaldo → Todo listo». NO verificado con clics reales: pasos TMDb/Biblioteca del asistente y «Usar otra carpeta…».
- Pruebas: `--ui-test wizard0|wizard1|wizard1new|wizard1locked|wizard5|wizardflow` (wizardflow escribe config.yaml junto al exe: borrarlo después).

## Empaquetado Windows (17/11/2026)
`npm run dist:win` / `scripts/dist-win.ps1 [-NoZip]` → `dist/CinemaLove-win/` y `dist/CinemaLove-win-<versión>.zip` (dist/ está en .gitignore). Contenido: cinemalove.exe + Qt (lo que dejó windeployqt) + libmpv-2.dll + `backend/{main.cjs,node.exe,node_modules/better-sqlite3 (lib + prebuilds/win32-x64.node)}` + `licencias/` + LEEME. ~266 MB sin comprimir. Requiere haber compilado antes (exe Release y `backend/dist/main.cjs`).
- Verificado: la distribución se ejecuta desde su carpeta con PATH mínimo (sin Qt, Node ni npm), levanta el backend incluido, hace el respaldo y no deja procesos huérfanos.
- NO verificado: instalación en una máquina limpia real (sin Visual C++ Redistributable ni drivers OpenGL actualizados), Linux (AppImage/tar.gz), firma del ejecutable.
- Para correr desde la distribución sin pasar por el asistente: no hace falta; sin `config.yaml` junto al exe se muestra el asistente.

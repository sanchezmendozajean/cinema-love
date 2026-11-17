# 20 · Plan de implementación

Cada fase termina con la app compilando, tests en verde y los criterios de aceptación verificados. No avanzar de fase con criterios pendientes sin avisar.

**Regla de oro de los datos**: durante todo el desarrollo, trabajar sobre **copias** de una base real (`.dev-data/`), nunca sobre la carpeta de datos que usa la versión Flutter.

## Fase 0 — Esqueleto
- Proyecto `app/` con electron-vite + React + TS + Tailwind + tokens de `design/10`.
- Ventana sin marco, barra de título propia, instancia única, preload con `contextBridge`, CSP.
- Fuentes e íconos locales.
- electron-builder generando ZIP para Windows y AppImage para Linux con un "hola mundo".
- **Aceptación**: `npm run dev` abre la ventana con HMR; `npm run dist:win` produce un ZIP que corre en una PC limpia.

## Fase 1 — Datos heredados
- `ConfigService` (`config.yaml`, rutas por tipo de distribución).
- `schema-v11.sql` volcado de una base real; apertura con verificaciones de `user_version`, respaldo, lock, PRAGMAs; tablas `cl_*`.
- `codecs.ts` + repositorios de lectura (libraries, items, series, seasons, extras, progress, watchlist, collections).
- Lector de la caché de imágenes de Flutter.
- **Aceptación**: tests que abren una copia de una base real creada por Flutter y leen todo correctamente (conteos iguales a los que muestra la app Flutter); una base creada desde cero por Electron se abre sin errores con la app Flutter.

## Fase 2 — Servidor interno y API de catálogo
- Fastify con listener interno + token admin; auth por API key/sesión.
- Endpoints v1-legado de catálogo, `/catalog`, `/home`, `/image`, `/me`, `/health`.
- `ApiClient` + hooks en el renderer; `getBootstrap()`.
- **Aceptación**: tests de contrato de los endpoints legado; `/catalog` con filtros combinados correcto y < 150 ms con 5 000 ítems.

## Fase 3 — Vistas de navegación
Implementar las vistas entregadas por Claude Design: shell, Inicio, Catálogo, Bibliotecas (solo lectura), fichas de película/serie/temporada/persona, Colecciones, Próximas a ver, tarjetas y menú ⋯ (acciones de progreso/listas funcionando).
- **Aceptación**: RF-01…RF-63 salvo administración; navegación fluida (60 fps en scroll de Catálogo con 2 000 pósters); estados de carga/vacío/error presentes.

## Fase 4 — Reproductor
- ffmpeg/ffprobe empaquetados; `MediaProbeService`; `playback-info`; sesiones remux/transcode; subtítulos a VTT; overlay de subtítulos; volumen 200 %; todos los atajos y OSD; progreso y reselección de pistas (compatibles con ids de mpv).
- **Aceptación**: matriz de pruebas con archivos de muestra: MP4 H.264/AAC (direct), MKV H.264/AC3 (remux), MKV HEVC 10-bit (transcode o direct según capacidades), AVI XviD (transcode), MKV con PGS (burn-in), SRT externo. Seek, cambio de pista y reanudación funcionan en todos. Progreso guardado en Electron se reanuda en Flutter y viceversa.

## Fase 5 — Administración
- Bibliotecas CRUD, escáner (workers, jobs, SSE), reconciliación, huérfanos, relink, homologar.
- TMDb: asistente masivo, búsqueda/edición manual, temporadas.
- **Aceptación**: escanear la misma carpeta con Flutter y con Electron produce las mismas `ruta_relativa`, `hash_respaldo`, temporadas y números de episodio; el matching automático da los mismos resultados en un set de 50 nombres de prueba.

## Fase 6 — Servidor público y Modo Cliente
- Listener público HTTP/HTTPS, certificados, puertos, mDNS, usuarios, token, sesiones activas, QR.
- Asistente de bienvenida completo; Modo Cliente (persistente y de sesión); `cliente_datos.sqlite`; confianza por huella; identidad visual violeta; reconexión.
- **Aceptación**: dos equipos en la LAN: B descubre a A por mDNS, se conecta con usuario y con token, navega, reproduce (direct y transcode), y el progreso queda donde corresponde según `03` §5. Un cliente Flutter (Android actual) se conecta al servidor Electron y reproduce.

## Fase 7 — Pulido y empaquetado
- Animaciones, accesibilidad de teclado (foco visible, navegación con flechas en grillas), rendimiento de imágenes (tamaños `w342` en grillas), logs, pantalla "Acerca de" con licencias.
- Linux: AppImage + tar.gz, probar en una distro común (Ubuntu/Fedora) y en una VM.
- **Aceptación**: checklist visual de `design/11` completo; builds autocontenidos en Windows y Linux.

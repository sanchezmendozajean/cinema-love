# CinemaLove (Qt 6 + libmpv) — Instrucciones para Claude Code

Versión de CinemaLove sobre **Qt 6** con **libmpv** (Windows y Linux). **Es la única versión vigente**: Electron y Flutter están obsoletas y solo son referencia histórica. **Proyecto autocontenido e independiente**: no lee, copia ni depende de ninguna otra carpeta (ni `electron-version/` ni `flutter-version/`) y no descarga nada de internet para compilarse ni ejecutarse; la única conexión a internet del producto es TMDb (datos y pósters de las películas).

- `specs/` — especificaciones de esta versión. **Fuente de verdad de lo que cambia** (reproductor, arquitectura, empaquetado). Empezar por `specs/README.md`.
- `specs/heredadas/` — especificaciones heredadas (datos, API, funcionalidades, diseño). Siguen valiendo en todo lo que `specs/` de aquí no cambia.
- `src/server/`, `src/client/`, `src/backend/` — backend **propio e integrado en C++** (API HTTP v1, escáner, TMDb, imágenes, servidor público con HTTPS/mDNS/QR, Modo Cliente). No hay Node ni proceso hijo (el sidecar Node se retiró el 18/11/2026; está en la etiqueta git `con-backend-node`). Ver `specs/03-backend-y-datos.md`.
- `third_party/` — binarios de terceros incluidos (libmpv LGPL, tipografías) con su procedencia y checksums en `third_party/README.md`. Qt, MSVC y CMake son la cadena de herramientas del equipo de desarrollo, no dependencias del proyecto.
- Estructura: `src/` (C++), `qml/` (interfaz), `cmake/`, `scripts/` (`dev.ps1`, `dist-win.ps1`, `dist-linux.sh`), `specs/` (incluye `specs/heredadas/`, copia propia de las specs de datos, API, funcionalidades y diseño).

## Idioma
Responder siempre en español: explicaciones, commits, comentarios y textos de la app.

## Personalidad
Colaborador perfeccionista con lo visual y amante del cine. Referencia: Jellyfin (grillas de pósters, backdrops grandes, filas por categoría), con más cuidado en el detalle. La metadata se presenta como un catálogo curado, no como una lista de archivos. Si algo visual queda mediocre, señalarlo y proponer mejoras.

## Reglas clave
- Leer `specs/07-brief-claude-code.md` (reglas de trabajo) antes de empezar.
- **Empezar por la Fase 0** (`specs/06-plan-de-implementacion.md`): es un spike que decide si se continúa. No avanzar si no pasa su aceptación.
- Los datos (`cinemalove.sqlite`, `cliente_datos.sqlite`, `images/`, `config.yaml`) son compatibles con los datos existentes de CinemaLove: **solo cambios aditivos** (`specs/heredadas/02-datos-y-compatibilidad.md`). **Política de datos reales**: al abrir una colección el backend hace un respaldo automático (`cinemalove.sqlite.bak-qt-AAAAMMDD`, cada 7 días, conserva 5) y toma el bloqueo de un solo escritor (`cinemalove.electron.lock` + `cinemalove.qt.lock`, por PID; el de Electron se respeta solo para no pisar datos que una instalación antigua aún tenga abiertos). El asistente de bienvenida elige la carpeta y la recuerda en `config.yaml` (`database_directory`). **Las pruebas automatizadas (`--selftest`, `--catalog-shot`, `--ui-test`, `--progress-test`, `--admin-scan`) solo se ejecutan sobre copias en `.dev-data/`** (el programa se niega en otro caso); el desarrollo diario con `scripts/dev.ps1` usa por defecto `.dev-data/cinemalove.sqlite`.
- La API HTTP v1 debe seguir siendo compatible con los clientes existentes (`specs/heredadas/04-api-servidor.md`).
- **Ventana y reproducción**: se recuerda tamaño, posición y si estaba maximizada o flotante (`window.ini`); la reproducción siempre empieza en pantalla completa y al salir se vuelve al estado de antes (`specs/01-arquitectura.md` §5).
- Nunca perder datos del usuario: toda operación destructiva pide confirmación explícita y, si se puede, ofrece primero una alternativa no destructiva.
- Modales: siempre con botón X visible; nunca se cierran al hacer clic afuera.
- **Autocontenido**: ninguna referencia a otras carpetas ni descargas (GitHub/etc.) para compilar o ejecutar; si se necesita un binario, se incluye en `third_party/` con checksum. Builds autocontenidos (Qt y libmpv incluidos; el backend va dentro del ejecutable); licencias LGPL (`specs/05-empaquetado-y-licencias.md`).
- Memoria del proyecto en `.claude/memory/` con `MEMORY.md` como índice.

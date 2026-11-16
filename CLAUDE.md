# CinemaLove (Qt 6 + libmpv) — Instrucciones para Claude Code

Nueva versión de CinemaLove sobre **Qt 6** con **libmpv** (Windows y Linux). Hermana de `../electron-version/` y `../flutter-version/`; el `CLAUDE.md` de la raíz del espacio de trabajo (`../CLAUDE.md`) da el contexto general.

- `specs/` — especificaciones de esta versión. **Fuente de verdad de lo que cambia** (reproductor, arquitectura, empaquetado). Empezar por `specs/README.md`.
- `../electron-version/specs/` — especificaciones heredadas (datos, API, funcionalidades, diseño). Siguen valiendo en todo lo que `specs/` de aquí no cambia.
- `../electron-version/` — versión Electron. Su backend (`src/main`) es el candidato a sidecar (`specs/03-backend-y-datos.md`). Solo referencia: no modificar desde aquí.
- `../flutter-version/` — versión Flutter. Solo referencia de lógica, datos y comportamiento; no modificar.
- El proyecto Qt (CMake, `src/`, `qml/`) se crea en esta raíz en la Fase 0.

## Idioma
Responder siempre en español: explicaciones, commits, comentarios y textos de la app.

## Personalidad
Colaborador perfeccionista con lo visual y amante del cine. Referencia: Jellyfin (grillas de pósters, backdrops grandes, filas por categoría), con más cuidado en el detalle. La metadata se presenta como un catálogo curado, no como una lista de archivos. Si algo visual queda mediocre, señalarlo y proponer mejoras.

## Reglas clave
- Leer `specs/07-brief-claude-code.md` (reglas de trabajo) antes de empezar.
- **Empezar por la Fase 0** (`specs/06-plan-de-implementacion.md`): es un spike que decide si se continúa. No avanzar si no pasa su aceptación.
- Los datos (`cinemalove.sqlite`, `cliente_datos.sqlite`, `images/`, `config.yaml`) se comparten con las otras versiones: **solo cambios aditivos** (`../electron-version/specs/02-datos-y-compatibilidad.md`). Desarrollar solo con copias en `.dev-data/`.
- La API HTTP v1 debe seguir siendo compatible con los clientes Flutter (`../electron-version/specs/04-api-servidor.md`).
- Nunca perder datos del usuario: toda operación destructiva pide confirmación explícita y, si se puede, ofrece primero una alternativa no destructiva.
- Modales: siempre con botón X visible; nunca se cierran al hacer clic afuera.
- Builds autocontenidos (Qt, libmpv y backend incluidos), sin dependencias externas en runtime; licencias LGPL (`specs/05-empaquetado-y-licencias.md`).
- Memoria del proyecto en `.claude/memory/` con `MEMORY.md` como índice.

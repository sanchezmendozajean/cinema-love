# CinemaLove (Qt 6 + libmpv) — Especificaciones

**CinemaLove sobre Qt 6 con libmpv** como motor de video. Es **la versión vigente** de CinemaLove: las versiones Electron y Flutter están obsoletas, se conservan archivadas (`.7z`) solo como referencia histórica y **no son dependencia de este proyecto** (ni de código, ni de carpetas, ni de compilación o ejecución). Mantiene la compatibilidad de datos (SQLite v11, `config.yaml`) y de API HTTP v1 con los clientes existentes. El motivo del cambio fue eliminar las pausas y reinicios de reproducción del modelo «`<video>` de Chromium + ffmpeg al vuelo» (ver `00-motivacion-y-decisiones.md`).

> **Estado: en desarrollo, con código.** El spike de la Fase 0 ya se hizo; el avance real está en `.claude/memory/`. Es una versión independiente con su propio repositorio.

## Cómo se relaciona con `../specs/heredadas/`

Estas specs **no repiten** lo que no cambia. Lo siguiente se hereda tal cual y sigue siendo la fuente de verdad:

| Se hereda de | Qué |
|---|---|
| `specs/heredadas/00-vision-y-alcance.md` | Visión, glosario, principios, plataformas (Windows y Linux x64) |
| `specs/heredadas/02-datos-y-compatibilidad.md` | Esquema SQLite v11, reglas aditivas, `config.yaml`, caché de imágenes |
| `specs/heredadas/03-modos-de-ejecucion.md` | Modo Local, Servidor, Modo Cliente, asistente de bienvenida |
| `specs/heredadas/04-api-servidor.md` | Contrato HTTP v1 (compatible con los clientes Flutter) |
| `specs/heredadas/06-bibliotecas-y-metadata.md` | Escáner, reconciliación, TMDb |
| `specs/heredadas/07-funcionalidades.md` | Requisitos funcionales RF-xx por pantalla |
| `specs/heredadas/design/` | Sistema de diseño, pantallas y componentes (se reimplementan en QML) |

**Lo que sí cambia** está en este directorio:

| Archivo | Contenido |
|---|---|
| `00-motivacion-y-decisiones.md` | Por qué existe, qué se gana y se pierde, decisiones abiertas |
| `01-arquitectura.md` | Stack Qt, estructura de carpetas, hilos, capas, CMake |
| `02-reproductor-mpv.md` | Integración de libmpv, interfaz del reproductor en QML, pistas y subtítulos, progreso |
| `03-backend-y-datos.md` | El backend integrado en C++ (API, escáner, TMDb, servidor público, Modo Cliente) y los datos |
| `04-interfaz-qml.md` | Traducción del sistema de diseño y las pantallas a QML; rendimiento de grillas |
| `05-empaquetado-y-licencias.md` | Windows, Linux, libmpv, Qt, licencias |
| `06-plan-de-implementacion.md` | Fases con criterios de aceptación; empieza por un *spike* de validación |
| `07-brief-claude-code.md` | Prompt de arranque y reglas de trabajo |

## Decisiones tomadas

1. **Qt 6 (última LTS al iniciar)**, C++17, CMake. Qt Quick/QML para la interfaz.
2. **libmpv** vía su API de render sobre OpenGL, integrado como elemento de escena de Qt Quick, de modo que los controles QML se dibujan **encima** del video sin ventanas hijas.
3. **Sin remux ni transcode en local**: mpv decodifica casi todo. El servidor siempre sirve el archivo original con `Range` (`/media/<id>/direct`).
4. Los datos (`cinemalove.sqlite`, `cliente_datos.sqlite`, `images/`, `config.yaml`) y la API HTTP v1 **no cambian** (reglas de `specs/heredadas/02` y `specs/heredadas/04`).

## Decisiones abiertas (resolver antes de la Fase 1)

Ver `00-motivacion-y-decisiones.md` §4: interfaz **QML nativa** frente a **web embebida (QtWebEngine)** (el backend se decidió: C++ integrado, sin Node).

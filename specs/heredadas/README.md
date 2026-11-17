# CinemaLove (Electron) — Especificaciones

Documentación para construir la nueva versión de CinemaLove sobre **Electron** (Windows y Linux), sucesora de la versión Flutter que vive en `../OldVersion/`.

- **Claude Design** construye las vistas a partir de la carpeta `design/`.
- **Claude Code** construye la aplicación a partir de todos los documentos, en el orden de `20-plan-de-implementacion.md`.

## Índice

| Archivo | Para quién | Contenido |
|---|---|---|
| `00-vision-y-alcance.md` | Todos | Qué es, qué cambia respecto a la versión Flutter, glosario |
| `01-arquitectura.md` | Claude Code | Stack, procesos de Electron, estructura de carpetas, capas |
| `02-datos-y-compatibilidad.md` | Claude Code | Esquema SQLite heredado (drift v11) y reglas para reutilizarlo sin romperlo; caché de imágenes TMDb; `config.yaml` |
| `03-modos-de-ejecucion.md` | Todos | Asistente de bienvenida, Modo Local, Modo Cliente, servidor embebido, descubrimiento en red |
| `04-api-servidor.md` | Claude Code | Contrato HTTP (compatible con los clientes Flutter existentes + extensiones). Es también el contrato del futuro cliente Android |
| `05-reproductor.md` | Todos | Motor híbrido (direct play + ffmpeg), controles, atajos, pistas, subtítulos |
| `06-bibliotecas-y-metadata.md` | Claude Code | Escaneo, convenciones de carpetas, reconciliación, TMDb |
| `07-funcionalidades.md` | Todos | Requisitos funcionales por pantalla (Inicio, Catálogo, fichas, colecciones, ajustes…) |
| `design/10-sistema-de-diseno.md` | Claude Design | Tokens de color, tipografía, espaciado, movimiento, iconografía |
| `design/11-pantallas.md` | Claude Design | Cada vista con su layout, contenido, estados y variantes |
| `design/12-componentes.md` | Claude Design | Catálogo de componentes reutilizables |
| `design/13-brief-claude-design.md` | Claude Design | Prompt listo para pegar + entregables esperados |
| `20-plan-de-implementacion.md` | Claude Code | Fases con criterios de aceptación |
| `21-brief-claude-code.md` | Claude Code | Prompt de arranque y reglas de trabajo |

## Decisiones ya tomadas (no reabrir sin consultar)

1. **Bibliotecas** (tipo Películas o Series, apuntan a una carpeta) se mantienen como en la versión Flutter. Las **Colecciones** personales también.
2. **Nueva vista "Catálogo"**: una única lista con *todos* los elementos (películas y series de todas las bibliotecas), con búsqueda, filtro por género y filtro por biblioteca.
3. **Reproductor híbrido**: `<video>` HTML5 cuando el archivo es compatible; remux/transcodificación con ffmpeg empaquetado cuando no.
4. **Base de datos compartida**: la app Electron abre el mismo `cinemalove.sqlite` (esquema drift v11) y la misma carpeta `images/`. Solo cambios aditivos. La versión Flutter debe poder seguir abriendo esos datos.
5. **API HTTP v1 compatible**: los clientes Flutter (Android incluido) existentes deben poder conectarse al servidor Electron.
6. Idioma de la UI: español.

## Referencias a la versión anterior

Cuando un documento dice "ver OldVersion", el código de referencia está en `../OldVersion/app/lib/` y la arquitectura en `../OldVersion/docs/ARQUITECTURA.md`. Ante una contradicción entre estas specs y OldVersion, **mandan estas specs**; si algo no está cubierto, se replica el comportamiento de OldVersion.

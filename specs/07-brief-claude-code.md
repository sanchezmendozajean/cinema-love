# 07 · Brief para Claude Code (alternativa Qt 6 + libmpv)

## Prompt de arranque (copiar y pegar)

> Vamos a construir la **alternativa Qt 6 + libmpv** de CinemaLove en la raíz de `qt6-version/`. Lee primero `CLAUDE.md` de esta carpeta, luego `specs/README.md` y todos los documentos que enlaza; las specs de `specs/heredadas/` se heredan salvo lo que `specs/` de aquí cambia. Las versiones Electron y Flutter están **obsoletas**: son solo referencia histórica (archivadas), no las modifiques y **nunca las uses como dependencia**; la versión que cuenta es esta. Trabaja por fases según `specs/06-plan-de-implementacion.md`, **empezando por la Fase 0 (spike)**. Al terminar cada fase, verifica sus criterios y resume qué quedó hecho y qué no. No continúes tras la Fase 0 si no pasa su aceptación: avísame con los resultados.

## Reglas de trabajo

1. **Idioma**: español en la UI, commits, comentarios razonables y respuestas.
2. **Aislamiento**: todo el código nuevo vive en `qt6-version/`. Este proyecto es independiente: no depende de ninguna otra carpeta ni la modifica; tampoco se tocan los datos reales del usuario.
3. **Fuente de verdad**: `specs/heredadas/` para datos, API, funcionalidades y diseño; `specs/` (de aquí) para lo que cambia (reproductor, arquitectura, empaquetado). Si algo contradice las specs, avisar antes de seguir.
4. **Datos del usuario**: trabajar solo con copias en `.dev-data/`; cambios de esquema solo aditivos (`specs/heredadas/02`). Nunca perder datos: toda operación destructiva pide confirmación y ofrece antes una alternativa no destructiva.
5. **API v1 compatible** con los clientes Flutter (`specs/heredadas/04`).
6. **Modales**: siempre con botón X visible; nunca se cierran al hacer clic afuera.
7. **Calidad visual**: implementar los diseños con fidelidad; señalar y proponer mejoras si algo queda mediocre. Verificar lo visual con capturas antes de dar algo por terminado.
8. **Autocontenido**: sin dependencias externas en runtime; cada librería vendorizada con checksum y licencia revisada (`05`).
9. **Robustez gráfica**: registrar errores del scene graph y de contexto GL; la ventana debe poder cerrarse aunque la escena falle.
10. **Memoria del proyecto**: mantener `.claude/memory/MEMORY.md` como índice de decisiones, resultados del spike, avances y quirks del entorno.
11. **Commits** pequeños por funcionalidad, referenciando `RF-xx` cuando aplique.

## Comandos esperados

| Comando | Qué hace |
|---|---|
| `cmake --preset <win\|linux>-debug` / `cmake --build --preset …` | Configurar y compilar |
| `ctest --preset …` | Qt Test + `qmltestrunner` |
| `scripts/dev.ps1` | Compila y lanza con la copia de `.dev-data/` |
| `scripts/dist-win.ps1` / `scripts/dist-linux.sh` | ZIP portable / AppImage + tar.gz |

## Entregable de la Fase 0 (resumen que debe devolver)
- Tabla de archivos probados (contenedor, códec, origen: local/SMB/HTTP) y resultado en Windows y Linux (X11/Wayland).
- Rendimiento: CPU/GPU, `hwdec` activo, fotogramas perdidos.
- Estabilidad: duración de la prueba larga y comportamiento de la ventana.
- Recomendación: continuar con mpv, usar plan B (Qt Multimedia) o descartar; y decisiones A/B de interfaz y backend.

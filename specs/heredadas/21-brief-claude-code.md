# 21 · Brief para Claude Code

## Prompt de arranque (copiar y pegar)

> Vamos a construir CinemaLove en Electron dentro de `app/`. Lee primero `CLAUDE.md` en la raíz y luego `specs/README.md` y todos los documentos que enlaza. La versión anterior en Flutter está en `OldVersion/` como referencia de lógica y datos. Los diseños de las vistas están en `specs/design/` (y, si existen, los prototipos exportados de Claude Design en `specs/design/prototipos/`). Trabaja por fases según `specs/20-plan-de-implementacion.md`, empezando por la Fase 0. Al terminar cada fase, verifica sus criterios de aceptación y resume qué quedó hecho y qué no.

## Reglas de trabajo

1. **Idioma**: español en la UI, mensajes de commit, comentarios cuando sea razonable y respuestas.
2. **Fuente de verdad**: `specs/`. Si una decisión de implementación contradice las specs, avisar antes de seguir. Si algo no está cubierto, replicar el comportamiento de `OldVersion/` y, si tampoco está ahí, preguntar.
3. **Datos del usuario**: nunca abrir la carpeta de datos real de la versión Flutter durante el desarrollo; trabajar con copias en `app/.dev-data/`. Nunca modificar el esquema heredado (ver `specs/02`).
4. **Paridad con Flutter**: escáner, hash, parsers de nombres y matching de TMDb se portan con sus casos de prueba; los resultados deben ser idénticos.
5. **Calidad visual**: implementar los diseños con fidelidad (espaciados, tipografía, estados). Si algo queda visualmente mediocre, señalarlo y proponer mejora.
6. **Modales**: siempre con X visible; nunca se cierran con clic afuera (`onPointerDownOutside`/`onInteractOutside` → `preventDefault` en Radix).
7. **Sin dependencias externas en runtime**: nada de CDNs, fuentes remotas ni ffmpeg del sistema.
8. **Memoria del proyecto**: mantener `app/.claude/memory/MEMORY.md` como índice de decisiones, avances y quirks del entorno (igual que en OldVersion).
9. **Commits** pequeños por funcionalidad, referenciando `RF-xx` cuando aplique.
10. **Verificar en vivo** lo visual (capturas con Playwright) antes de dar algo por terminado.

## Comandos esperados en `app/package.json`

| Script | Qué hace |
|---|---|
| `dev` | electron-vite dev con HMR |
| `build` | compila main/preload/renderer |
| `test` / `test:e2e` | Vitest / Playwright |
| `lint` / `typecheck` | |
| `dist:win` | ZIP portable x64 (`dist/CinemaLove-Windows-x64-<versión>.zip`) |
| `dist:linux` | AppImage + tar.gz x64 |
| `fetch:ffmpeg` | descarga los binarios estáticos de ffmpeg/ffprobe por plataforma a `resources/ffmpeg/` (con verificación de checksum) |

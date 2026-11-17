# Backend de CinemaLove (Qt 6)

Código **propio** de esta versión (TypeScript, Node). Sirve la API HTTP v1, el escáner de bibliotecas, TMDb, imágenes, administración y el servidor público. La app Qt lo lanza como proceso hijo (`src/backend/BackendLauncher.cpp`) y habla con él por HTTP en 127.0.0.1.

- Nació como una copia del backend de la versión Electron (16/11/2026) y **desde entonces es independiente**: no se sincroniza con ninguna otra carpeta y los cambios se hacen aquí.
- Autocontenido: usa el Node de `../third_party/node/` y las dependencias de `node_modules/` (incluidas en la carpeta; no hace falta `npm install` ni internet).
- La única conexión a internet del producto es TMDb (datos y pósters de las películas).

## Compilar (sin internet)
```
..\third_party\node\node.exe scripts\build.mjs      # → dist\main.cjs (o: ..\scripts\build-backend.ps1)
```
`dist/main.cjs` se empaqueta con esbuild; `better-sqlite3` (módulo nativo) se carga desde `node_modules/`.

## Entorno
Variables que pone la app: `CL_DATA_DIR` (carpeta con `cinemalove.sqlite`), `CL_APP_ROOT`, `CL_SIN_FFMPEG=1`. Protocolo de arranque por stdout: `CL_READY {...}` / `CL_ERROR {...}`; se cierra con SIGTERM, con la orden `quit` por stdin o si se cierra stdin.

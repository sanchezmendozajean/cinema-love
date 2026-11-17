# 01 · Arquitectura

## 1. Stack

| Capa | Elección | Motivo |
|---|---|---|
| Shell | Electron (última estable al iniciar el proyecto) | Windows + Linux con un solo código |
| Lenguaje | TypeScript estricto (`strict: true`) en todos los procesos | |
| Build | `electron-vite` | Dev server con HMR para el renderer, bundling de main/preload |
| Empaquetado | `electron-builder` | Windows: ZIP portable + `portable` exe. Linux: AppImage + tar.gz |
| UI | React 19 + React Router | |
| Datos en UI | TanStack Query (caché de requests) + Zustand (estado de UI) | |
| Estilos | Tailwind CSS v4 sobre CSS variables (tokens de `design/10-sistema-de-diseno.md`) | Los tokens viven en un solo archivo `tokens.css` |
| Componentes base | Radix UI primitives (diálogos, menús, selects, tooltips) estilizados a mano | Accesibilidad sin imponer estética |
| Animación | Framer Motion (o `motion`) | Transiciones de página, hover de tarjetas |
| Iconos | **Material Symbols Rounded** (fuente local) — un único set en toda la app | Continuidad con la versión Flutter |
| Fuentes | Empaquetadas localmente (`@fontsource`), nunca desde la red | Offline |
| SQLite | `better-sqlite3` (proceso main) | Síncrono, rápido, transacciones simples |
| Servidor HTTP | `fastify` + `@fastify/cors` | Rendimiento y streaming con Range |
| Hash de contraseñas | `bcryptjs` | Compatible con los hashes `$2a$/$2b$` generados por el paquete `bcrypt` de Dart |
| Certificados HTTPS | `node-forge` (autofirmado) | Puro JS |
| mDNS | `bonjour-service` | Descubrir servidores en la LAN |
| Video | `<video>` HTML5 + `ffmpeg`/`ffprobe` estáticos empaquetados (`extraResources`) | Ver `05-reproductor.md` |
| Tests | Vitest (unidad), Playwright para Electron (e2e) | |
| Lint/format | ESLint + Prettier | |

## 2. Modelo de procesos

```
┌──────────────────────────── Proceso main (Node) ───────────────────────────┐
│  AppCore                                                                   │
│   ├─ ConfigService        (config.yaml, rutas de datos)                    │
│   ├─ Database             (better-sqlite3 → cinemalove.sqlite)             │
│   ├─ ClientDatabase       (better-sqlite3 → cliente_datos.sqlite)          │
│   ├─ Repositorios         (libraries, items, series, progress, …)          │
│   ├─ ScannerService       (hashing en worker_threads)                      │
│   ├─ MetadataService      (TMDb)                                           │
│   ├─ ImageCacheService    (descarga y sirve imágenes de TMDb)              │
│   ├─ MediaProbeService    (ffprobe, caché de resultados)                   │
│   ├─ PlaybackService      (sesiones ffmpeg: remux/transcode)               │
│   └─ HttpApp (fastify)    ── mismas rutas, dos "listeners":                │
│        • Listener interno   127.0.0.1:<puerto efímero>, token admin        │
│        • Listener público   0.0.0.0:<puerto configurable>, auth cliente    │
│  WindowManager, Tray (opcional), DiscoveryService (mDNS)                   │
└────────────────────────────────────────────────────────────────────────────┘
          ▲ IPC (solo arranque/ventana/diálogos nativos)      ▲ HTTP
          │                                                    │
┌──── preload (contextBridge) ────┐           ┌──── Renderer (React) ────┐
│ window.cinemalove = {           │──────────▶│ ApiClient(baseUrl,token) │
│   getBootstrap(), pickFolder(), │           │  - Modo Local: listener  │
│   window controls, openExternal │           │    interno               │
│ }                               │           │  - Modo Cliente: servidor│
└─────────────────────────────────┘           │    remoto                │
                                              └──────────────────────────┘
```

### Decisión clave: el renderer siempre habla HTTP

En **Modo Local** el proceso main levanta un *listener interno* en `127.0.0.1` con puerto efímero y un **token de administrador** aleatorio por sesión (nunca persistido, nunca expuesto en la red). El renderer obtiene `{baseUrl, token, modo}` por IPC (`getBootstrap()`) y desde ahí usa el mismo `ApiClient` que usaría en Modo Cliente.

Consecuencias:

- **Una sola implementación de la UI** para Local y Cliente: solo cambia `baseUrl`, el token y los permisos (`capabilities`, ver `04-api-servidor.md` §3).
- El `<video>` siempre recibe una URL HTTP, también en local (necesario para remux/transcode).
- El servidor público se prueba solo con el uso diario.
- Las rutas de administración (`/api/v1/admin/...`) solo se registran/aceptan con el token admin del listener interno. El listener público **rechaza** el token admin aunque alguien lo obtuviera.

### IPC (mínimo, tipado)

IPC solo para lo que HTTP no puede hacer: diálogos nativos (elegir carpeta/archivo), controles de ventana, abrir enlaces externos, reiniciar la app, leer el bootstrap, cambiar de modo (que reconfigura el `ApiClient`). Definir los canales en `src/shared/ipc.ts` con tipos compartidos. `contextIsolation: true`, `nodeIntegration: false`, `sandbox: true` en el renderer.

## 3. Estructura del repositorio

El proyecto nuevo vive en `D:\Coding\CinemaLove\app\` (hermano de `OldVersion/` y `specs/`).

```
app/
├─ electron.vite.config.ts
├─ electron-builder.yml
├─ resources/
│   ├─ icon.png / icon.ico
│   └─ ffmpeg/{win-x64,linux-x64}/{ffmpeg,ffprobe}[.exe]
├─ src/
│   ├─ shared/                  # tipos y contratos compartidos main↔renderer
│   │   ├─ api-types.ts         # DTOs de 04-api-servidor.md
│   │   ├─ ipc.ts
│   │   └─ enums.ts             # TipoBiblioteca, EstadoArchivo, EstadoVisto (valores = los de drift)
│   ├─ main/
│   │   ├─ index.ts             # arranque, single-instance lock
│   │   ├─ config/              # config.yaml, rutas
│   │   ├─ db/
│   │   │   ├─ database.ts      # apertura, PRAGMAs, migraciones aditivas
│   │   │   ├─ schema-v11.sql   # referencia del esquema heredado
│   │   │   ├─ migrations/      # solo tablas/columnas cl_* nuevas
│   │   │   └─ codecs.ts        # fechas (epoch seg), bools, JSON, enums
│   │   ├─ repositories/
│   │   ├─ services/            # scanner, metadata, tmdb, images, probe, playback, discovery
│   │   ├─ server/
│   │   │   ├─ app.ts           # fastify + plugins
│   │   │   ├─ auth.ts
│   │   │   ├─ routes/          # libraries, items, series, catalog, home, progress, collections, watchlist, playback, images, admin
│   │   │   └─ certs.ts
│   │   └─ window/
│   ├─ preload/index.ts
│   └─ renderer/
│       ├─ index.html
│       └─ src/
│           ├─ app/             # router, shell, providers
│           ├─ api/             # ApiClient + hooks de TanStack Query
│           ├─ features/        # welcome, home, catalog, libraries, detail, series, person, collections, watchlist, player, settings
│           ├─ components/      # ver design/12-componentes.md
│           └─ styles/tokens.css
└─ tests/
```

## 4. Capas y reglas

- **Repositorios** (main) encapsulan SQL. Nunca SQL en rutas HTTP.
- **Rutas HTTP** validan entrada (JSON schema de fastify), resuelven el *principal* (admin local / API key / usuario) y llaman a repositorios/servicios.
- **Renderer** nunca conoce rutas de disco salvo en vistas de administración que las reciben del endpoint admin.
- Operaciones largas (escaneo, importación masiva de metadata) son **jobs** con id: `POST` crea el job, `GET /jobs/<id>` o un stream SSE (`/api/v1/admin/jobs/<id>/events`) informa progreso; se pueden cancelar.
- El hashing de archivos (64 KB inicio + 64 KB fin) corre en un pool de `worker_threads` para no bloquear el event loop del main (que también sirve video).
- Todo texto visible al usuario, en español, centralizado en `renderer/src/i18n/es.ts` (permite traducir luego).

## 5. Ventana

- Ventana sin marco (`frame: false`) con barra de título propia, integrada al diseño. En Windows usar `titleBarOverlay` para los botones nativos (minimizar/maximizar/cerrar) si el resultado visual es bueno; en Linux, botones propios.
- Tamaño mínimo 1024×640. Recordar tamaño/posición/maximizado entre sesiones (en `cl-config.json`, ver `02` §1).
- Título: `CinemaLove` en Modo Local, `CinemaLove Client` en Modo Cliente.
- **Instancia única** (`app.requestSingleInstanceLock()`): una segunda ejecución enfoca la ventana existente. Dos instancias sobre la misma base romperían el supuesto de un único escritor.
- El reproductor usa pantalla completa real de la ventana (`setFullScreen`).

## 6. Seguridad

- CSP estricta en el renderer: `default-src 'self'`; `img-src`/`media-src` permiten el `baseUrl` activo y `blob:`; nada de orígenes externos (las imágenes de TMDb llegan por el proxy `/api/v1/image`).
- Validar que toda ruta física pedida por la API esté dentro de la carpeta de su biblioteca (evitar path traversal). Las rutas físicas nunca se exponen al listener público.
- Certificados autofirmados: el cliente solo los acepta si el usuario marcó "Confiar en este certificado" tras ver la huella SHA-256 (ver `03`). Implementar con `session.setCertificateVerifyProc` limitado a ese host+huella; nunca desactivar la verificación global.

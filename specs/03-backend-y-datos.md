# 03 · Backend y datos

La interfaz Qt es solo el cliente de la API v1 (`specs/heredadas/04`). Falta decidir **qué sirve esa API**.

## 1. Opciones

| | **A. Sidecar Node (recomendada para empezar)** | **B. Port a C++/Qt** |
|---|---|---|
| Qué es | El backend, hoy **código propio** de esta versión en `backend/` (nació de una copia del de la versión Electron y ya no se sincroniza con ella): Fastify, `better-sqlite3`, escáner, TMDb, imágenes, descubrimiento) se ejecuta como **proceso hijo** empaquetado con la app, sin ventana | Se reescriben servidor HTTP, repositorios, escáner y TMDb en C++ (QtHttpServer, QtSql, QNetworkAccessManager) |
| Reutiliza | Casi todo; los tests de contrato y paridad ya existen | Nada; hay que reescribir y reprobar |
| Esfuerzo | Bajo: lanzar, vigilar y cerrar el proceso | Muy alto (la mayor parte del trabajo de las Fases 1, 2, 5 y 6 de `specs/heredadas/20`) |
| Dependencias en runtime | Un runtime Node empaquetado (autocontenido, sin instalar nada) | Ninguna adicional |
| Tamaño | +~60–90 MB (Node + `node_modules` + better-sqlite3) | Menor |
| Riesgo | Dos procesos y su ciclo de vida; una app «Qt + Node» menos elegante | Divergencia de comportamiento respecto a la versión de referencia; paridad con Flutter por reprobar |
| Mantenimiento | Un solo backend para las dos UIs (Electron y Qt) | Dos backends a mantener |

**Recomendación:** A. Permite validar lo importante (reproducción y UI) sin reescribir un backend que ya funciona. El port a C++ se decide más tarde, con datos, si el tamaño o el ciclo de vida del sidecar resultan un problema.

> Consecuencia a tener en cuenta: el backend sidecar sigue conteniendo `PlaybackService` y sus sesiones ffmpeg para los clientes Flutter/Android. La UI Qt **no** las usa. El backend se extrae a un paquete independiente de Electron (sin `electron` como dependencia) para poder ejecutarlo con Node plano. Ese refactor es la primera tarea de la opción A (Fase 1 de `06`).

## 2. Opción A en detalle

### Ciclo de vida (`BackendLauncher`)
1. Al arrancar la app Qt: elegir puerto efímero y generar el **token admin** aleatorio de la sesión.
2. Lanzar `node backend/main.js` (`QProcess`) con variables de entorno: carpeta de datos, token, puerto interno, ruta de ffmpeg/ffprobe incluidos, `CL_HEADLESS=1`.
3. Esperar `GET /api/v1/health` (con límite de tiempo) antes de mostrar la UI; si falla, pantalla de error con el log del proceso.
4. El proceso hijo se cierra con la app (cierre ordenado por señal; y *job object* en Windows / `PR_SET_PDEATHSIG` en Linux para que no quede huérfano si la app muere).
5. Si el backend muere durante la ejecución: reintentar una vez automáticamente; si vuelve a fallar, avisar con opción de reiniciar. Los datos nunca se pierden por esto (SQLite transaccional).
6. **Instancia única**: la protege la app Qt; el backend no abre la base si hay otro escritor (mismas reglas de `specs/heredadas/02`).

### Distribución
`backend/` junto al ejecutable: `node` (binario), `main.js` empaquetado (esbuild/`@vercel/ncc`) y `better-sqlite3` precompilado para esa plataforma y versión de Node. Sin npm ni Node del sistema.

### Lo que el backend deja de hacer para esta UI
- Nada para la UI Qt: usa los mismos endpoints de catálogo, progreso, administración y `media/<id>/direct`.

## 3. Datos y compatibilidad

Sin cambios: `cinemalove.sqlite` (drift v11, solo cambios aditivos), `cliente_datos.sqlite`, `images/`, `config.yaml` (`specs/heredadas/02`). Mientras la app Qt y la Electron coexistan, **no pueden abrir la misma carpeta a la vez** (un solo escritor); el bloqueo de `specs/heredadas/02` se respeta entre las tres versiones.

Durante el desarrollo se trabaja con **copias** en `.dev-data/` (regla de oro de `specs/heredadas/20`).

## 4. Opción B en detalle (si se aprueba más adelante)

Orden sugerido de portado, cada paso verificable contra el backend Node con los mismos tests de contrato:
1. `ConfigService` y apertura de SQLite (`QSQLITE`) con PRAGMAs, `user_version`, respaldo y lock.
2. Repositorios de lectura → endpoints de catálogo, `/home`, `/image`.
3. Progreso, colecciones, próximas a ver.
4. Servidor público (HTTP/HTTPS, certificados autofirmados, mDNS).
5. Escáner (hash 64 KB + 64 KB, paridad con Flutter), TMDb, administración.
6. Retirar el sidecar.

Los DTOs (`specs/heredadas/04` §4) se definen una vez en C++ y se serializan con `QJsonObject`; los tests de paridad del escáner (50 nombres de prueba) se portan tal cual.

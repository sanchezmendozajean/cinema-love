# 01 · Arquitectura

## 1. Stack

| Capa | Elección | Motivo |
|---|---|---|
| Framework | **Qt 6** (última LTS al iniciar), módulos Core, Gui, Qml, Quick, QuickControls2 (estilo `Basic`, personalizado), Network, Sql, Svg | Windows + Linux con un solo código |
| Lenguaje | C++17 (lógica y reproductor) + QML/JS (interfaz) | |
| Build | CMake ≥ 3.24 + presets (`CMakePresets.json`) | Estándar de Qt 6 |
| Video | **libmpv** (API `client` + `render` OpenGL) | Ver `02` |
| HTTP cliente | `QNetworkAccessManager` | Un único `ApiClient` para Local y Cliente |
| JSON | `QJsonDocument` | |
| SQLite | `QtSql` (driver `QSQLITE`) solo si hay port a C++ (`03` opción B) | |
| Imágenes | `QQuickImageProvider` asíncrono con caché en disco y en memoria | Ver `04` |
| Fuentes e iconos | Empaquetados como recursos Qt (`.qrc`): fuentes locales y **Material Symbols Rounded** | Offline, continuidad con las otras versiones |
| Tests | Qt Test (unidad/lógica), `qmltestrunner` (componentes QML) | |
| Estilo de código | `clang-format`, `clang-tidy` | |
| Ventana | `QQuickWindow` con `Qt.FramelessWindowHint` y barra de título propia | Igual que en Electron |

## 2. Modelo de procesos e hilos

```
┌─────────────────────── CinemaLove (proceso Qt) ───────────────────────┐
│  Hilo principal / GUI                                                  │
│   ├─ QML (shell, vistas, controles del reproductor)                    │
│   ├─ Modelos C++ expuestos a QML (CatalogModel, HomeModel, …)          │
│   ├─ ApiClient  ── HTTP ──▶ backend (ver abajo)                        │
│   └─ MpvItem (QQuickFramebufferObject)  ← dibuja en el hilo de render │
│  Hilo de render de Qt Quick   (GL: aquí se llama a mpv_render_context) │
│  Hilo de eventos de mpv       (mpv_wait_event → señales Qt)            │
│  QThreadPool                  (decodificar imágenes, hashing si C++)   │
└───────────────────────────────────────────────────────────────────────┘
              │ HTTP (127.0.0.1:<puerto efímero> + token admin)
              ▼
┌──────────── Backend (ver 03) ────────────┐
│ Opción A: proceso hijo Node (sidecar)    │
│ Opción B: módulos C++ dentro del proceso │
└──────────────────────────────────────────┘
```

### Decisión clave heredada: la UI siempre habla HTTP
Igual que en `specs/heredadas/01`: en Modo Local el backend levanta un listener interno en `127.0.0.1` con token de administrador aleatorio por sesión; en Modo Cliente apunta a un servidor remoto. **Una sola implementación de la UI.** La diferencia con Electron es que el reproductor ya no necesita que esa URL sea un stream de ffmpeg: recibe una URL (o ruta) del archivo original.

## 3. Estructura del repositorio

```
qt6-version/                   # raíz del repositorio (el proyecto Qt se crea aquí en la Fase 0)
├─ specs/                       # este directorio
├─ CMakeLists.txt
├─ CMakePresets.json
├─ cmake/                    # búsqueda de libmpv, helpers de despliegue
├─ third_party/              # libmpv (headers + binarios por plataforma), si se vendorizan
├─ resources/
│   ├─ fonts/  icons/  qml.qrc
│   └─ icon.png / icon.ico
├─ src/
│   ├─ main.cpp              # arranque, instancia única, backend, ventana
│   ├─ app/                  # AppController, Bootstrap, WindowController, Settings
│   ├─ api/                  # ApiClient, DTOs, serialización
│   ├─ models/               # CatalogModel, HomeModel, ProgressModel… (QAbstractListModel)
│   ├─ player/               # MpvItem, MpvController, TrackModel, SubtitleSettings
│   ├─ images/               # ImageProvider + caché
│   ├─ backend/              # BackendLauncher (sidecar) o módulos C++ (opción B)
│   └─ util/
├─ qml/
│   ├─ Main.qml
│   ├─ theme/                # Tokens.qml (singleton), tipografía, motion
│   ├─ components/           # ver design/12-componentes.md
│   ├─ views/                # Welcome, Home, Catalog, Libraries, Detail, Series, Person, Collections, Watchlist, Settings
│   └─ player/               # PlayerView.qml, Controls.qml, Osd.qml, SubtitleOverlay.qml
└─ tests/
```

## 4. Capas y reglas

- **Modelos C++** (`QAbstractListModel`) alimentan las listas de QML. QML nunca hace HTTP ni toca SQLite.
- **ApiClient** es el único que conoce URLs y tokens. Expone métodos asíncronos que devuelven resultados por señal; los modelos los consumen.
- **El reproductor no sabe de catálogo**: recibe `PlaybackRequest { fuente, titulo, inicioSegundos, pistaAudio, pistaSubtitulo }` y emite eventos (`posicion`, `pausa`, `finArchivo`, `error`).
- **Todo texto visible** en español, centralizado (`qsTr` con un único contexto o un archivo de cadenas), preparado para traducir.
- **Operaciones largas** (escaneo, metadata) son jobs del backend, con progreso por SSE o sondeo (`specs/heredadas/01` §4).
- **Modales**: siempre con botón X visible; no se cierran al hacer clic afuera (`closePolicy: Popup.CloseOnEscape` como máximo si el diseño lo permite; nunca `CloseOnPressOutside`). Regla del proyecto.

## 5. Ventana

- Sin marco, barra de título propia con botones de minimizar, maximizar y cerrar (arrastre con `startSystemMove()`, redimensionado con `startSystemResize()`, ambos de `QWindow`).
- Tamaño mínimo 1024×640; recordar tamaño, posición y estado maximizado (mismo `cl-config.json`).
- **Instancia única** (bloqueo con `QLockFile` + canal local `QLocalServer` para enfocar la existente).
- Pantalla completa real con `showFullScreen()`.
- **Robustez gráfica** (lección de la versión Flutter, donde la ventana quedaba gris sin controles): el cierre y la barra de título deben funcionar aunque la escena falle; registrar `QQuickWindow::sceneGraphError` y `graphicsResetStatus`, y exponer un modo de renderizado alternativo (`QT_QUICK_BACKEND=software`) en Ajustes → Avanzado.

## 6. Seguridad

Igual que `specs/heredadas/01` §6, adaptado: validar rutas dentro de la carpeta de su biblioteca, no exponer rutas físicas al listener público, aceptar certificados autofirmados solo con huella confirmada por el usuario (`QSslConfiguration` + `sslErrors` limitado a host + huella; nunca `ignoreSslErrors()` global).

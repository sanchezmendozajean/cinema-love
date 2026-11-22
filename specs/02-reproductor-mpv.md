# 02 · Reproductor con libmpv

Reemplaza a `specs/heredadas/05-reproductor.md` §1 (motor híbrido). El resto de ese documento (controles, atajos, OSD, subtítulos, progreso, ids de pista compatibles con mpv) **se mantiene**, con las adaptaciones de abajo.

## 1. Modelo de reproducción

Un solo modo: **mpv reproduce el archivo original**.

| Situación | Fuente que recibe mpv |
|---|---|
| Modo Local, archivo en este equipo | Ruta del archivo (`file`), resuelta por el backend |
| Modo Local, archivo en red (SMB/UNC) | Ruta tal cual; mpv la lee directamente |
| Modo Cliente | `GET /api/v1/media/<id>/direct` (con `Range`, token en cabecera) |

- **Se eliminan** remux, transcode, sesiones ffmpeg, `PlaybackService`, ventanas de bytes y 416.
- El endpoint `direct` del servidor sirve el archivo original con `Range` completo y `Content-Type` correcto. Los clientes Flutter y los reproductores externos siguen usando lo que ya existe (la API v1 no cambia); los endpoints de sesiones ffmpeg pueden mantenerse en el backend para esos clientes.
- mpv gestiona el buffer de red (`cache`, `demuxer-max-bytes`, `demuxer-readahead-secs`). Parámetros iniciales: `cache=yes`, `demuxer-max-bytes=150MiB`, `demuxer-readahead-secs=60`; ajustables.
- `ffprobe` deja de ser necesario para decidir el modo. Sigue pudiendo usarse para `playback-info` si los clientes Flutter lo piden; en esta app basta con que mpv informe de las pistas (`track-list`).

## 2. Integración con Qt Quick

### 2.1 Render
- `MpvItem` hereda de `QQuickFramebufferObject`: mpv dibuja en un FBO en el hilo de render de Qt Quick con `mpv_render_context` (API `OPENGL`). Los controles QML van encima como cualquier otro `Item`, **sin ventanas hijas ni transparencias especiales**.
- Qt 6 usa por defecto un backend gráfico distinto de OpenGL en Windows (D3D11). Hay que forzar OpenGL antes de crear la ventana:
  `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)` (y `QSurfaceFormat` con perfil core 3.3 o superior).
- `mpv_render_context_set_update_callback` solicita un repintado; el callback solo emite una señal hacia el hilo de render (nunca llamar a mpv desde dentro del callback).
- Registrar `get_proc_address` con `QOpenGLContext::getProcAddress`.
- Opciones de mpv fijas: `vo=libmpv`, `hwdec=auto-safe`, `keep-open=yes`, `input-default-bindings=no`, `input-vo-keyboard=no`, `osc=no`, `osd-level=0` (el OSD y los subtítulos los dibuja la app).
- **Plan B si el spike falla** (ver `06` Fase 0): `Qt Multimedia` con el backend FFmpeg de Qt 6. Menor control y compatibilidad que mpv, pero integración nativa sin FBO. Se documenta el resultado del spike en la memoria del proyecto.

### 2.2 Hilos y eventos
- Un `QThread` dedicado ejecuta `mpv_wait_event`. Cada evento relevante se reenvía por señal (`Qt::QueuedConnection`) al hilo principal.
- Propiedades observadas (`mpv_observe_property`): `time-pos`, `duration`, `pause`, `volume`, `mute`, `track-list`, `aid`, `sid`, `paused-for-cache`, `seeking`, `eof-reached`, `hwdec-current`, `video-params`.
- Los comandos (`loadfile`, `seek`, `set_property`) se envían de forma asíncrona (`mpv_command_async`, `mpv_set_property_async`).

### 2.3 API expuesta a QML (`MpvController`)
Propiedades: `posicion`, `duracion`, `pausado`, `volumen` (0–150), `silenciado`, `buffering`, `pistasAudio`, `pistasSubtitulo`, `audioActivo`, `subtituloActivo`, `infoTecnica`, `estado` (`inactivo|cargando|reproduciendo|pausado|fin|error`).
Métodos: `cargar(PlaybackRequest)`, `reproducirPausar()`, `buscar(segundos, relativo)`, `siguientePista…`, `elegirAudio(id)`, `elegirSubtitulo(id|"no")`, `detener()`.
Señales: `eventoGuardarProgreso(motivo)`, `finDeArchivo()`, `errorReproduccion(mensaje)`.

## 3. Cambios respecto a `specs/heredadas/05`

| `specs/heredadas/05` | En esta alternativa |
|---|---|
| Direct/Remux/Transcode, matriz Chromium, `caps` del cliente | No aplica: mpv reproduce todo. Se conserva el campo informativo «modo» como `Directo (mpv)` + `hwdec` activo |
| Timeline «real» con `offsetSegundos` | No aplica: `time-pos` ya es la posición real |
| Seek fuera del buffer → nueva sesión | No aplica: mpv hace seek en el archivo |
| Cambio de pista → nueva sesión | `aid`/`sid` en caliente |
| Subtítulos → WebVTT + overlay propio | mpv renderiza los subtítulos (texto y de imagen, incluido ASS con estilos). Ver §4 |
| Volumen hasta 150 % con `GainNode` | `volume` de mpv (`volume-max=150`) |
| Arrastrar la ventana desde el video con IPC y `setPosition` | `startSystemMove()` de `QWindow` desde un `MouseArea` con umbral de 4 px; no hay conflicto con `-webkit-app-region` |
| Info técnica | Se alimenta de `video-params`, `audio-params`, `hwdec-current`, `track-list` |
| HDR → tone mapping con ffmpeg | `tone-mapping` y `target-colorspace-hint` de mpv; opción en Ajustes |
| Máx. 2 transcodes | No aplica |

## 4. Subtítulos y pistas

- **Ids de pista = ids de mpv** (como ya exigía `specs/heredadas/05` §3 para compatibilidad con Flutter): 1-based por tipo, `auto` y `no`. Aquí no hay mapeo: son los de mpv directamente. Los subtítulos externos se cargan con `sub-add` y mpv los numera después de los internos, igual que en la versión Flutter.
- **Subtítulos**: mpv los dibuja (`sub-ass`, `sub-font`, `sub-border-size`, `sub-shadow-offset`). Se mapean las claves heredadas `subtitle_font_size` y `subtitle_vertical_offset` a `sub-font-size` y `sub-pos`. El estilo por defecto sigue siendo texto blanco con contorno y sombra, sin recuadro.
- Los subtítulos de imagen (PGS, VobSub) **ya no fuerzan transcode**: se muestran tal cual.
- El modal «Configuración de subtítulos» mantiene el comportamiento de `specs/heredadas/05` §2: vista previa sobre el video real.
- Subtítulos externos junto al archivo (`pelicula.es.srt`, `pelicula.forced.srt`): mpv los detecta con `sub-auto=fuzzy`; el backend sigue pudiendo listarlos para la ficha.

## 5. Progreso y reanudación

Igual que `specs/heredadas/05` §4: guardar **solo** en pausa, seek, cierre, cambio de pista y al superar el 95 % (→ `visto`). Nunca periódico.

Al reanudar: `loadfile` con `start=<segundos>`, y reselección **una sola vez** de audio y subtítulo guardados cuando llegue el evento `track-list` (aquí sí hace falta esperar a ese evento; en Flutter se usaba un temporizador de 800 ms, que no hay que replicar).

## 6. Controles, atajos y OSD

Se mantienen **íntegros** los de `specs/heredadas/05` §5 (teclado, ratón, volumen hasta 150 %, OSD, barra de controles, fin de reproducción), implementados en QML:

- Los atajos se capturan en `Keys` del `PlayerView`, nunca por las teclas por defecto de mpv (`input-default-bindings=no`).
- La barra de controles, el OSD y el overlay de «Esc de nuevo» son `Item`s QML por encima del `MpvItem`.
- **La reproducción siempre arranca en pantalla completa** (ver `01` §5); la salida normal (`F`, `Esc`, doble clic) deja la ventana flotante o maximizada, como estuviera antes.
- Clic simple muestra/oculta controles (no pausa), doble clic pantalla completa, controles se ocultan a los 30 s, cursor oculto a los 5 s solo si los controles están ocultos (`cursorShape` + `MouseArea`).
- Línea de tiempo con zona de buffer (`demuxer-cache-state`) y *hover preview* de tiempo **con fotograma** (`Thumbnailer`, `src/player/Thumbnailer.*`): una segunda instancia de mpv sin ventana ni audio, en un hilo de prioridad baja, dibuja con el renderizador por software de libmpv (`MPV_RENDER_API_TYPE_SW`) miniaturas de 320 px de ancho. Salta al fotograma clave más cercano (`hr-seek=no`, sin `hwdec`), agrupa los instantes en cubos de 5 s, guarda hasta 240 miniaturas en memoria y atiende como mucho una petición cada 120 ms (gana la más reciente). Se sirven a QML por `image://thumb/<generación>/<cubo>`. Solo con archivos locales: en Modo Cliente (URL) no hay fotograma, solo el tiempo. Medido: ~280 ms por miniatura en un MP4 1080p.

## 7. Errores y recuperación

- `error`/`end-file` con razón `error` → pantalla de error del reproductor con detalle técnico copiable. Reintentar vuelve a cargar en la última posición.
- `paused-for-cache` → indicador de carga sin reiniciar nada. Solo en Modo Cliente por red lenta; en local no debería aparecer.
- Un fallo del contexto GL (`graphicsResetStatus`) → recrear el `mpv_render_context` conservando la posición, sin cerrar el reproductor.

## 8. Qué no resuelve esta alternativa

- En **Modo Cliente por internet o Wi-Fi débil**, mpv reproduce el archivo original por HTTP: si el bitrate supera el ancho de banda, habrá `paused-for-cache`. Para ese caso el servidor puede seguir ofreciendo las sesiones transcode de la API v1 (opcional, fase posterior).
- Los clientes Flutter/Android no cambian.

# 05 · Reproductor

## 1. Motor híbrido

El reproductor es un `<video>` HTML5 dentro de la app (nunca un reproductor externo). Cada reproducción elige uno de tres modos, decidido en el **servidor** (local o remoto) a partir de ffprobe:

| Modo | Cuándo | Qué hace | Costo |
|---|---|---|---|
| **Direct** | Contenedor y códecs reproducibles por Chromium | Sirve el archivo tal cual con `Range` (`/media/<id>/direct`) | Ninguno |
| **Remux** | Video compatible, contenedor o audio no (MKV con H.264 + AC3/DTS, por ejemplo) | `ffmpeg -c:v copy`, audio → AAC estéreo (o copy si es compatible), salida fMP4 fragmentado | Bajo |
| **Transcode** | Video no compatible (HEVC sin soporte, MPEG-2, VC-1, XviD…) o subtítulo de imagen quemado | Video → H.264 (`libx264 -preset veryfast`, o encoder por hardware si `cl.hw_accel` y está disponible: NVENC/QSV/AMF/VAAPI), audio → AAC | Alto |

### Matriz de compatibilidad (Chromium/Electron)
- Contenedores: `mp4`, `m4v`, `webm`, `mov` (con códecs compatibles).
- Video: H.264 (hasta High 4:2:0 8-bit), VP8, VP9, AV1. HEVC: **detectar en tiempo de ejecución** con `MediaSource.isTypeSupported('video/mp4; codecs="hvc1.1.6.L120.90"')` (depende de hardware/SO); el cliente informa sus capacidades.
- Audio: AAC, MP3, Opus, Vorbis, FLAC. AC3/E-AC3/DTS/TrueHD → remux con audio a AAC.
- El cliente envía sus capacidades en `caps` (lista de códecs soportados) para que el servidor decida — un cliente Android futuro tendrá otras.

### Remux/transcode: flujo progresivo con reinicio en *seek*
Primera versión (simple y robusta):
1. `POST /playback/sessions` con `inicioSegundos` → el servidor lanza `ffmpeg -ss <inicio> -i <archivo> … -f mp4 -movflags frag_keyframe+empty_moov+default_base_moof pipe:1` y devuelve `url` + `offsetSegundos`.
2. El `<video>` reproduce ese stream. La UI muestra la línea de tiempo **real** del archivo: `posiciónReal = offsetSegundos + video.currentTime`; la duración viene de ffprobe, no del `<video>`.
3. Un *seek* fuera de lo ya almacenado en el buffer → `DELETE` de la sesión y nueva sesión con el nuevo `inicioSegundos`. Mostrar spinner sobre el último frame.
4. Cambiar de pista de audio (o activar un subtítulo de imagen) en remux/transcode → nueva sesión en la posición actual con `-map` de la pista elegida.
5. Las sesiones mueren con `DELETE`, al cerrar el reproductor o tras 60 s sin lectura. Máx. 2 transcodes simultáneos por servidor (configurable); los extra responden 503 con mensaje claro.

Evolución futura (no v1): HLS bajo demanda con segmentos para seek sin reinicio.

### Direct play: pistas de audio
Chromium solo expone `video.audioTracks` con el flag `AudioVideoTracks`: activar `app.commandLine.appendSwitch('enable-blink-features', 'AudioVideoTracks')`. Si el archivo direct tiene varias pistas y el cambio no es posible por esa vía, pasar a **remux** con la pista elegida.

## 2. Subtítulos

- **Texto** (SRT, ASS/SSA, mov_text, WebVTT) internos o externos (`pelicula.srt`, `pelicula.es.srt`, `pelicula.forced.srt` junto al video, mismo nombre base): se convierten a **WebVTT** en el servidor (`/media/<id>/subtitles/<n>.vtt`) y se renderizan con un overlay propio (no el estilo nativo de `<track>`), leyendo `TextTrack.cues`. ASS se muestra sin estilos avanzados en v1.
- **Imagen** (PGS, VobSub, DVB): requieren *burn-in* → fuerzan transcode. Avisar en el menú: "Este subtítulo requiere convertir el video (más uso de CPU)".
- Estilo del overlay: texto blanco con **contorno y sombra**, nunca recuadro sólido detrás. Tamaño y posición vertical configurables (claves heredadas `subtitle_font_size` y `subtitle_vertical_offset` en Modo Local; en Modo Cliente, preferencia local del equipo).
- El modal "Configuración de subtítulos" muestra la vista previa **sobre el video real** (el modal es pequeño y deja ver el video detrás), sin texto de muestra dentro del modal.

## 3. `playback-info` (ffprobe resumido)

```json
{
  "duracionSegundos": 8132.4,
  "contenedor": "matroska",
  "tamanoBytes": 4123456789,
  "video": {"codec":"hevc","perfil":"Main 10","ancho":3840,"alto":1606,"hdr":"hdr10","fps":23.976},
  "audio": [
    {"id":"1","indice":1,"codec":"eac3","canales":6,"idioma":"spa","titulo":"Latino","defecto":true},
    {"id":"2","indice":2,"codec":"aac","canales":2,"idioma":"eng","titulo":null,"defecto":false}
  ],
  "subtitulos": [
    {"id":"1","codec":"subrip","idioma":"spa","titulo":"Forzados","forzado":true,"tipo":"texto","externo":false},
    {"id":"ext-1","codec":"subrip","idioma":"spa","titulo":"pelicula.es.srt","tipo":"texto","externo":true}
  ],
  "decision": {"modo":"remux","motivo":"Audio E-AC3 no compatible"}
}
```

- **Ids de pista compatibles con mpv** (la versión Flutter guardó `pista_audio_id`/`pista_subtitulo_id` con ids de mpv): numeración **1-based por tipo** en el orden del contenedor; valores especiales `auto` y `no`. Para subtítulos externos, mpv los numera después de los internos: mapear `ext-k` ↔ `(internos + k)` al leer/escribir progreso. Así reanudar respeta la elección hecha en cualquiera de las dos versiones.
- Cachear en `cl_media_probe` (invalidar por tamaño/mtime).
- HDR → si se transcodifica, tone-mapping a SDR (`zscale`+`tonemap` si el build de ffmpeg lo trae; si no, aviso).

## 4. Progreso

Guardar (`PUT progress`) **solo** en estos eventos: pausa, *seek* (teclado, clic o arrastre), cerrar el reproductor, cambio de pista y al superar el 95 % (→ `visto`). **Nunca** periódicamente (decisión heredada: evitar escrituras constantes). Perder el progreso por un cierre forzado es aceptable. Excepción nueva: en Modo Cliente, si el stream se corta por red, guardar la última posición conocida al reconectar.

Al reanudar: seek a la posición guardada y reselección **una sola vez** de audio/subtítulo guardados, esperando a conocer la lista real de pistas (en Electron la lista viene de `playback-info`, así que no hace falta el temporizador de 800 ms que necesitaba mpv).

Dentro de una colección, el progreso es el de esa colección (`collections/<id>/progress/<itemId>`), nunca el global. "Próximas a ver" usa el global.

## 5. Controles e interacción (heredado de Flutter, mantener)

### Teclado
| Tecla | Acción |
|---|---|
| `Espacio` | Play/pausa (único atajo que alterna) |
| `→` / `←` | ±10 s |
| `Shift+→` / `Shift+←` | ±30 s |
| `↑` / `↓` | Volumen ±5 % (hasta **150 %**). Nunca mueve la posición ni muestra la barra de controles |
| `F` o doble clic | Pantalla completa ↔ ventana |
| `M` | Silenciar |
| `Esc` | 1ª vez: toast "Presiona Esc de nuevo para salir" (3 s). 2ª dentro de esa ventana: guardar progreso y salir. Si está en pantalla completa, la 1ª pulsación además sale de pantalla completa |
| `N` (episodios) | Siguiente episodio |

El indicador de salto **acumula**: dos `→` seguidos muestran "+20 s" mientras el indicador siga visible y la dirección sea la misma.

### Mouse
- **Clic simple** sobre el video: muestra/oculta controles. **No** pausa.
- **Doble clic**: pantalla completa.
- Mover el mouse **no** muestra controles. Los controles visibles se ocultan tras **30 s** sin interacción.
- Cursor: se oculta tras 5 s quieto **solo** si los controles están ocultos.
- Rueda sobre el video: volumen.

### Volumen hasta 150 %
`MediaElementAudioSourceNode` → `GainNode` (0–2.0) → destino. El slider y el OSD muestran 0–150 %. Persistir el último volumen por equipo.

### OSD (indicadores flotantes)
- Volumen: esquina superior derecha, 1800 ms.
- Play/pausa/salto: centro, 1050 ms.
- Escala grande (ícono ~40 px, texto ~30 px) para leer desde el sillón. Ningún OSD muestra u oculta la barra de controles.

### Barra de controles
Título (y `T1 · E3 — Nombre del episodio` en series), línea de tiempo con buffer y *hover preview* de tiempo, play/pausa, ±10 s, volumen (slider + %), tiempo actual / restante (clic alterna), selector de **audio**, selector de **subtítulos** (incluye "Desactivados" y "Configuración…"), siguiente episodio, info técnica (ícono "i": modo Direct/Remux/Transcode, resolución, códecs, motivo), pantalla completa, cerrar.

### Fin de reproducción
- Película: al terminar, pantalla final con backdrop y "Volver a la ficha".
- Episodio: a falta de 20 s (o en créditos si se conocen), tarjeta "Siguiente episodio en 10 s" con cancelar.

## 6. Ventana del reproductor

El reproductor ocupa toda la ventana, por encima del shell y sin la barra de título de la app. En modo ventana, se puede **arrastrar la ventana desde cualquier punto del video**, sin depender de que los controles estén visibles. Ojo: `-webkit-app-region: drag` se traga los clics del DOM en Windows (rompería clic/doble clic sobre el video), así que implementar el arrastre a mano: `mousedown` + umbral de 4 px de movimiento → IPC `window:drag-start/move` que mueve la ventana con `setPosition`; si no se superó el umbral, es un clic normal.

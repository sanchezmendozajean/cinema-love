# 04 · API HTTP del servidor

Esta API la usan: (1) el propio renderer en Modo Local (listener interno), (2) otras instancias Electron en Modo Cliente, (3) los clientes Flutter existentes (incluido el APK Android actual) y (4) el futuro cliente Android. Es **el contrato** del ecosistema.

## 1. Convenciones

- Base: `/api/v1`. JSON UTF-8. Claves en `camelCase` (como en Flutter).
- Errores: `{"error": "mensaje en español"}` con el status HTTP correspondiente (400, 401, 403, 404, 409, 500).
- Fechas en JSON: ISO 8601 (`creadoEn`).
- Enums en JSON: los mismos nombres que en la base (`pelicula`, `serie`, `noVisto`, `enProgreso`, `visto`).
- Nunca exponer `ruta_actual`, `ruta_relativa` ni `hash_respaldo` en rutas públicas. Las rutas `/admin/*` (solo listener interno) sí pueden.
- **Compatibilidad**: los endpoints marcados **[v1-legado]** existen en la versión Flutter y deben responder con **la misma forma** (se pueden agregar campos, nunca quitar ni renombrar). Referencia: `OldVersion/app/lib/core/server/`.

## 2. Autenticación

- Credencial en header `X-Api-Key: <valor>` o query `?api_key=<valor>` (la query es necesaria para `<video src>` y `<img src>`).
- El valor puede ser: la API key activa, un token de sesión (`sesiones`, vigente) o —solo en el listener interno— el token admin efímero.
- Resolución del *principal*:

| Credencial | Principal | `usuarioId` para progreso/colecciones |
|---|---|---|
| Token admin (interno) | `admin` | `NULL` (admin local) |
| API key | `token` | `NULL` (comparte la fila del admin local, igual que Flutter) |
| Token de sesión | `usuario` | id del usuario |

- Públicas (sin credencial): `GET /health`, `POST /login`.
- Endpoints de usuario (watchlist, collections) devuelven **403** con principal `token` [v1-legado] — en ese caso el cliente usa su base local.

## 3. Endpoints

### Sistema
| Método y ruta | Descripción |
|---|---|
| `GET /health` [v1-legado] | `{"status":"ok"}` + **nuevos**: `"app":"cinemalove"`, `"serverId"`, `"nombre"`, `"apiVersion":1`, `"plataforma":"electron"` |
| `POST /login` [v1-legado] | Body `{nombreUsuario, password}` → `{token, usuarioId, nombreUsuario}`. 401 si inválido |
| `POST /logout` | Invalida el token de sesión actual |
| `GET /me` | `{principal: "admin"\|"token"\|"usuario", nombreUsuario?, capabilities: [...]}`. Capabilities: `browse`, `play`, `progress`, `userLists` (watchlist/colecciones en servidor), `admin` |

### Catálogo [v1-legado]
| Ruta | Respuesta |
|---|---|
| `GET /libraries` | `[Library]` |
| `GET /libraries/<id>/items` | `[MediaItem]` (películas de esa biblioteca, sin episodios) |
| `GET /libraries/<id>/series` | `[Serie]` |
| `GET /items/<id>` | `MediaItem` detalle |
| `GET /items/<id>/extras` | `[Extra]` |
| `GET /series/<id>` | `Serie` detalle |
| `GET /series/<id>/seasons` | `[Season]` |
| `GET /series/<id>/episodes` | `[MediaItem]` episodios |
| `GET /media/<id>/stream` | Archivo crudo con soporte `Range` (lo usan los clientes Flutter con mpv) |
| `GET /extras/<id>/stream` | Ídem para extras |

Los ítems con `estado_archivo = noEncontrado` no se listan.

### Catálogo unificado (nuevo)
`GET /catalog` — alimenta la vista **Catálogo**. Devuelve películas y series (no episodios) de todas las bibliotecas.

Query:
| Parámetro | Ejemplo | Notas |
|---|---|---|
| `q` | `padrino` | Busca en título, título original (sin acentos ni mayúsculas) |
| `generos` | `Drama,Crimen` | Lista; **OR** dentro del filtro |
| `bibliotecas` | `<id>,<id>` | Lista; OR |
| `tipo` | `pelicula` \| `serie` | Opcional |
| `anioDesde`, `anioHasta` | `1970`, `1979` | |
| `estado` | `noVisto` \| `enProgreso` \| `visto` | Solo aplica a películas; requiere principal con progreso |
| `orden` | `titulo` \| `-anio` \| `-agregado` \| `-reproducido` | Prefijo `-` = descendente. Por defecto `titulo` |
| `pagina`, `tamano` | `1`, `60` | Máx. 200 |

Filtros distintos se combinan con **AND**.

Respuesta:
```json
{
  "total": 431,
  "pagina": 1,
  "tamano": 60,
  "elementos": [
    { "kind": "pelicula", "id": "…", "libraryId": "…", "titulo": "El Padrino", "anio": 1972,
      "posterUrl": "https://image.tmdb.org/t/p/w500/…", "generos": ["Drama","Crimen"],
      "duracionSegundos": 10500, "progreso": {"estado":"enProgreso","posicionSegundos":3600} },
    { "kind": "serie", "id": "…", "libraryId": "…", "titulo": "Dark", "anio": 2017,
      "posterUrl": "…", "generos": ["Drama"], "temporadas": 3, "episodios": 26, "episodiosVistos": 10 }
  ],
  "facetas": {
    "generos":    [{"valor":"Drama","conteo":210}, …],
    "bibliotecas":[{"id":"…","nombre":"Películas","tipo":"pelicula","conteo":380}, …],
    "anios":      {"min":1931,"max":2026}
  }
}
```
Las facetas se calculan sobre el resultado filtrado **sin** su propio filtro (patrón estándar de facetas) para que el usuario vea cuántos resultados daría cada opción.

Implementación: consulta SQL con `UNION ALL` de `media_items` (películas: `series_id IS NULL`) y `series`; géneros vía `json_each(generos)`. Normalización de texto para búsqueda en JS o con una función SQLite registrada (`unaccent_lower`).

### Inicio (nuevo, corrige limitación de Flutter)
`GET /home` → `{continuarViendo:[…], proximasAVer:[…], agregadosRecientemente:[…], vistosRecientemente:[…], destacado: {…}}` calculado para el principal (admin/token → `usuarioId NULL`; usuario → su id). Episodios en "Continuar viendo" incluyen `serie {id,titulo,posterUrl}` y `SxxEyy`. `destacado`: el ítem para el hero de Inicio (último en progreso; si no hay, uno reciente con backdrop).

### Progreso [v1-legado]
| Ruta | Notas |
|---|---|
| `GET /media/<id>/progress` | `{posicionSegundos, estado, pistaAudioId?, pistaSubtituloId?}` |
| `PUT /media/<id>/progress` | Body igual. `estado: "visto"` → marcar visto (y retirar de Próximas a ver); `"noVisto"` → reiniciar; `"enProgreso"`/ausente → guardar posición |
| `GET /libraries/<id>/progress-states` (nuevo) | `{ "<mediaItemId>": "enProgreso" \| "visto" }` (ausencia = no visto) |

### Próximas a ver [v1-legado]
`GET /watchlist` → `[MediaItem]` · `PUT /watchlist/<mediaItemId>` · `DELETE /watchlist/<mediaItemId>` → `{ok:true}`. 403 con token.

### Colecciones [v1-legado]
`GET/POST /collections` (body `{nombre, quitarAlVerse}`) · `PUT/DELETE /collections/<id>` · `GET /collections/item/<mediaItemId>` (colecciones que contienen el ítem) · `GET/POST /collections/<id>/items` (body `{mediaItemId}`) · `DELETE /collections/<id>/items/<mediaItemId>` · `GET /collections/<id>/continuar-viendo` · `GET/PUT /collections/<id>/progress/<mediaItemId>`. 403 con token.
Nuevo: `GET /collections` incluye `conteo` y `posters` (hasta 4 URLs para el mosaico de la tarjeta).

### Personas (nuevo)
`GET /people/<tmdbPersonId>/in-catalog` → películas/series del catálogo donde aparece (en reparto o equipo). Resuelve la limitación de Flutter en Modo Cliente. La biografía/foto de la persona se pide a TMDb desde el servidor: `GET /people/<tmdbPersonId>` (cacheado en memoria; requiere API key de TMDb en el servidor).

### Imágenes (nuevo)
`GET /image?src=<url TMDb codificada>&size=w342` → bytes de la imagen con `Cache-Control: max-age=31536000, immutable`. Ver `02` §8. Requiere credencial (por query `api_key`).

### Reproducción (nuevo) — ver `05-reproductor.md`
| Ruta | Descripción |
|---|---|
| `GET /media/<id>/playback-info` | Resultado de ffprobe resumido: contenedor, duración, pistas de video/audio/subtítulos (con índices compatibles con mpv), y la **decisión** sugerida (`direct`, `remux`, `transcode`) para las capacidades que declare el cliente (`?caps=` JSON base64 opcional) |
| `POST /playback/sessions` | Body `{mediaItemId \| extraId, inicioSegundos, audio, subtitulo, modo?, maxAltura?}` → `{sessionId, url, modo, offsetSegundos, subtitulos:[{id, url, idioma, titulo}]}` |
| `GET /playback/sessions/<sid>/stream` | Stream fMP4 progresivo (remux/transcode) |
| `DELETE /playback/sessions/<sid>` | Detiene ffmpeg. También se detiene solo tras 60 s sin lectura |
| `GET /media/<id>/subtitles/<n>.vtt` | Subtítulo n (texto) convertido a WebVTT (cacheado en `transcode/`). `n` = índice mpv o `ext-<k>` para `.srt` externos |
| `GET /media/<id>/direct` | Igual que `/stream` pero con `Content-Type` correcto para `<video>` |

### Administración (solo listener interno, principal `admin`)
`/admin/libraries` (CRUD; `PUT` con cambio de ruta dispara reconciliación) · `/admin/libraries/<id>/scan` (job) · `/admin/libraries/<id>/metadata/scan-missing` · `/scan-all` (jobs) · `/admin/libraries/<id>/orphans` (`GET` listar, `DELETE` borrar con `?confirm=<conteo>`) · `/admin/items/<id>/metadata` (`PUT` editar, `POST /search` buscar en TMDb, `POST /assign`) · `/admin/items/<id>/file` (relink, homologar) · `/admin/series/<id>/metadata…` · `/admin/users` (CRUD) · `/admin/api-key` (`GET`, `POST /regenerate`) · `/admin/server` (estado, start/stop, config, certificado) · `/admin/sessions` (streaming activos) · `/admin/settings` (TMDb key, etc.) · `/admin/jobs/<id>` y `/admin/jobs/<id>/events` (SSE) · `DELETE /admin/jobs/<id>` (cancelar).

## 4. DTOs

Definir en `src/shared/api-types.ts`. Formas legado (de `OldVersion/app/lib/core/server/json_mappers.dart`):

```ts
type Library   = { id; nombre; tipo: 'pelicula'|'serie'; creadoEn };
type MediaItem = { id; libraryId; seriesId|null; numeroTemporada|null; numeroEpisodio|null;
                   tmdbId|null; titulo|null; anio|null; posterUrl|null; backdropUrl|null;
                   duracionSegundos|null; creadoEn;
                   // detalle:
                   tituloOriginal?; idiomaOriginal?; sinopsis?; estudio?; generos?: string[];
                   reparto?: Persona[]; equipo?: Persona[] };
type Serie     = { id; libraryId; tmdbId; titulo; anio; posterUrl; backdropUrl; creadoEn; /* + detalle igual */ };
type Season    = { id; seriesId; numeroTemporada; titulo; sinopsis; posterUrl };
type Extra     = { id; mediaItemId; titulo };
type Collection= { id; nombre; quitarAlVerse; creadoEn };
type Persona   = { id?: number; nombre: string; rol?: string; fotoUrl?: string };
type Progreso  = { posicionSegundos: number; estado: EstadoVisto; pistaAudioId?: string; pistaSubtituloId?: string };
```
Campos nuevos permitidos en respuestas legado: `generos` en la versión resumida de `MediaItem`/`Serie` (útil para filtros), `progreso` resumido.

## 5. Requisitos no funcionales del servidor

- `Range` correcto en todos los streams (206, `Content-Range`, `Accept-Ranges`).
- CORS: permitir cualquier origen solo para `GET` de imágenes/streams; el resto, sin CORS (los clientes son apps, no páginas web).
- Rate limit suave en `/login` (5 intentos/minuto por IP).
- Logs de acceso a un archivo rotativo en la carpeta de datos (`logs/`), sin credenciales.
- Tests de contrato: un set de requests/respuestas grabadas contra el servidor Flutter (si se puede) o derivadas de su código, ejecutadas contra el servidor Electron.

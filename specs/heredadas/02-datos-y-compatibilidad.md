# 02 · Datos y compatibilidad con la versión Flutter

La app Electron **reutiliza** los datos de la versión Flutter tal cual: la base `cinemalove.sqlite`, la base `cliente_datos.sqlite`, la carpeta `images/` y el archivo `config.yaml`. Ambas versiones deben poder abrir los mismos datos (no a la vez).

## 1. Carpeta de datos y `config.yaml`

### Ubicación de `config.yaml`

Junto al ejecutable, igual que en Flutter (Windows/Linux). En Electron, "junto al ejecutable" se resuelve así:

| Distribución | Carpeta |
|---|---|
| ZIP / carpeta desempaquetada (Windows, Linux tar.gz) | `path.dirname(process.execPath)` |
| Windows `portable` de electron-builder | `process.env.PORTABLE_EXECUTABLE_DIR` |
| Linux AppImage | `path.dirname(process.env.APPIMAGE)` (el `execPath` apunta a un montaje de solo lectura) |
| Desarrollo (`electron-vite dev`) | `app/.dev-data/` (en `.gitignore`) |

Si esa carpeta no tiene permiso de escritura, avisar al usuario y ofrecer usar `app.getPath('userData')`.

### Formato (idéntico a Flutter)

Modo Local:
```yaml
database_directory: "D:/CinemaLoveData"
```
Modo Cliente persistente:
```yaml
modo: cliente
servidor_id: "6f1c…"   # fila de cliente_servidores
```
En Modo Cliente persistente, la carpeta de datos (donde vive `cliente_datos.sqlite`) es la misma carpeta de `config.yaml`.

Flutter ignora claves desconocidas al leer, pero **reescribe el archivo entero** al guardar (perdería cualquier clave extra). Por eso Electron escribe `config.yaml` solo con estas claves, en el mismo formato, y guarda lo propio de cada equipo (tamaño/posición de ventana, densidad de grilla, volumen) en `cl-config.json` junto a `config.yaml`.

### Contenido de la carpeta de datos

```
<database_directory>/
├─ cinemalove.sqlite            # biblioteca (Modo Local)
├─ cliente_datos.sqlite         # estado de este equipo como cliente
├─ images/                      # caché de imágenes TMDb (formato Flutter)
│   ├─ imageCacheInfo.json
│   ├─ <archivos de flutter_cache_manager>
│   └─ cl/                      # NUEVO: imágenes descargadas por Electron
├─ certs/                       # NUEVO: certificado autofirmado del servidor (si no existía)
└─ transcode/                   # NUEVO: temporales de ffmpeg (se limpia al arrancar)
```

## 2. Reglas de compatibilidad (obligatorias)

1. **No modificar** tablas, columnas, índices ni `PRAGMA user_version` del esquema heredado (drift **v11**). Drift usa `user_version` para decidir migraciones; si Electron lo cambiara, la app Flutter intentaría "migrar" y podría romper.
2. Todo lo nuevo va en **tablas nuevas con prefijo `cl_`**. La versión del esquema propio de Electron se guarda en `cl_meta(clave='schema_version')`.
3. Escribir en las tablas heredadas respetando exactamente las codificaciones de drift (§4).
4. Al abrir una base por primera vez desde Electron, hacer **una copia de respaldo** `cinemalove.sqlite.bak-electron-AAAAMMDD` (copia, nunca mover).
5. Al abrir:
   - `user_version == 11` → OK.
   - `user_version < 11` → no abrir; mensaje: "Esta base fue creada con una versión antigua de CinemaLove. Ábrela una vez con la versión Flutter para actualizarla." (Migrar desde Electron queda fuera de alcance.)
   - `user_version > 11` → abrir **solo lectura** con aviso (hay una versión Flutter más nueva; no arriesgar escrituras).
   - Base inexistente (instalación nueva) → crear el esquema v11 exacto (`schema-v11.sql`) + `user_version = 11` + tablas `cl_*`. Así la versión Flutter también puede abrir una base creada por Electron.
6. PRAGMAs por conexión: `foreign_keys = ON` (las cascadas del esquema dependen de esto), `busy_timeout = 5000`. **No** cambiar `journal_mode` (dejar el que tenga el archivo).
7. Una sola app escribiendo a la vez: no abrir Flutter y Electron simultáneamente sobre la misma carpeta. Electron crea `cinemalove.electron.lock` mientras está abierta y avisa si encuentra uno de un proceso vivo.

> **Primer paso de Claude Code:** volcar el esquema real con `sqlite3 cinemalove.sqlite .schema` sobre una **copia** de una base real y guardarlo como `src/main/db/schema-v11.sql`. La tabla de abajo es la referencia lógica (derivada de `OldVersion/app/lib/data/database/tables.dart`); el `.schema` real manda en tipos, defaults y CHECKs.

## 3. Esquema heredado — `cinemalove.sqlite` (drift v11)

Nombres de tabla y columna en `snake_case` (drift convierte `rutaRelativa` → `ruta_relativa`).

### `libraries`
| columna | tipo | notas |
|---|---|---|
| id | TEXT PK | UUID v4 |
| nombre | TEXT | 1–200 chars |
| tipo | TEXT | `pelicula` \| `serie` |
| ruta_actual | TEXT | ruta absoluta, editable |
| creado_en | INTEGER | epoch segundos |

### `media_items` (películas y episodios)
| columna | tipo | notas |
|---|---|---|
| id | TEXT PK | UUID |
| library_id | TEXT FK→libraries CASCADE | |
| ruta_relativa | TEXT | relativa a la raíz de la biblioteca, separador `/` siempre |
| hash_respaldo | TEXT NULL | ver §5 |
| estado_archivo | TEXT | `ok` \| `noEncontrado` |
| series_id | TEXT NULL FK→series CASCADE | solo episodios |
| numero_temporada, numero_episodio | INTEGER NULL | solo episodios |
| tmdb_id | TEXT NULL | |
| titulo, titulo_original | TEXT NULL | |
| idioma_original | TEXT NULL | ISO 639-1 |
| anio | INTEGER NULL | |
| sinopsis, estudio | TEXT NULL | |
| poster_url, backdrop_url | TEXT NULL | URL completa de TMDb (`https://image.tmdb.org/t/p/w500/...`, backdrop `w1280`, still de episodio `w300`) |
| duracion_segundos | INTEGER NULL | |
| generos | TEXT NULL | JSON `["Drama","Crimen"]` |
| reparto | TEXT NULL | JSON `[{"id":123,"nombre":"…","rol":"…","fotoUrl":"https://image.tmdb.org/t/p/w185/…"}]` (leer también la clave legado `personaje` como `rol`) |
| equipo | TEXT NULL | mismo formato que `reparto` (dirección, guion, producción) |
| creado_en, actualizado_en | INTEGER | epoch segundos |

### `series`
Igual que `media_items` en metadata (`tmdb_id, titulo, titulo_original, idioma_original, anio, sinopsis, estudio, poster_url, backdrop_url, generos, reparto, equipo`) más `id, library_id, ruta_relativa` (carpeta de la serie), `estado_archivo`, `creado_en, actualizado_en`. Sin `hash_respaldo` ni duración.

### `seasons`
`id` PK, `series_id` FK CASCADE, `numero_temporada` (UNIQUE con `series_id`), `tmdb_id, titulo, sinopsis, poster_url, reparto, equipo` (NULL; `reparto/equipo` guardados pero no usados en la UI), `creado_en, actualizado_en`.

### `extras`
`id` PK, `media_item_id` FK CASCADE (la película), `ruta_relativa`, `hash_respaldo`, `estado_archivo`, `titulo` (derivado del nombre de archivo), `creado_en`.

### `watch_progresses` (progreso global)
`id` INTEGER PK AUTOINCREMENT, `media_item_id` FK CASCADE, `usuario_id` FK→usuarios CASCADE NULL (**NULL = admin local**), `posicion_segundos` INTEGER default 0, `estado` (`noVisto` \| `enProgreso` \| `visto`), `ultima_reproduccion` INTEGER NULL, `pista_audio_id`, `pista_subtitulo_id` TEXT NULL.
Sin UNIQUE en la tabla: **una fila por (media_item_id, usuario_id)** se garantiza en código (upsert manual comparando `usuario_id IS ?`).

### `proximas_a_ver`
`id` AUTOINCREMENT, `media_item_id` FK, `usuario_id` NULL, `agregado_en`. Una fila por (ítem, usuario), garantizado en código.

### `collections`
`id` TEXT PK, `nombre` (1–200), `quitar_al_verse` BOOL default 0, `usuario_id` NULL, `creado_en`.

### `collection_items`
`id` AUTOINCREMENT, `collection_id` FK, `media_item_id` FK, `agregado_en`. UNIQUE(`collection_id`, `media_item_id`).

### `collection_watch_progresses`
Como `watch_progresses` + `collection_id` FK.

### `usuarios`
`id` PK, `nombre_usuario` UNIQUE, `password_hash` (bcrypt), `rol` (`cliente`), `creado_en`.

### `sesiones`
`token` PK, `usuario_id` FK CASCADE, `creada_en`, `expira_en` (30 días, sin renovación deslizante).

### `api_keys`
`id` PK, `valor` UNIQUE, `creado_en`, `activa` BOOL. Se usa la única activa.

### `app_settings` (clave/valor)
Claves heredadas: `tmdb_api_key`, `subtitle_font_size`, `subtitle_vertical_offset`, `server_http_port`, `server_https_port`, `server_mode` (`http`\|`https`). Claves nuevas de Electron: prefijo `cl.` (p. ej. `cl.server_name`, `cl.server_autostart`, `cl.hw_accel`, `cl.discovery_enabled`).

## 4. Codificaciones de drift (respetarlas al escribir)

| Tipo Dart | En SQLite | Ejemplo |
|---|---|---|
| `DateTime` | INTEGER, **epoch en segundos** (UTC) | `1756339200` |
| `bool` | INTEGER 0/1 | |
| `textEnum` | TEXT con el **nombre** del enum en camelCase | `noEncontrado`, `enProgreso`, `pelicula` |
| Listas | TEXT con JSON | `["Drama"]` |
| IDs | UUID v4 en minúscula | |

Centralizar en `src/main/db/codecs.ts`. Tests unitarios que lean una base real de ejemplo y verifiquen ida y vuelta.

> Verificar en el `.schema` real que las fechas sean INTEGER (default de drift sin `store_date_time_values_as_text`). Si fueran TEXT, ajustar codecs.

## 5. Identidad de archivos (idéntica a Flutter)

- `ruta_relativa`: `path.relative(rutaBiblioteca, archivo)` con `\` reemplazado por `/`.
- `hash_respaldo`: SHA-256 (hex minúscula) de `primeros 64 KiB` ‖ `últimos 64 KiB` (solo si el archivo supera 64 KiB) ‖ `UTF-8(String(tamañoEnBytes))`. Ver `_hashDeRespaldo` en `OldVersion/app/lib/data/services/library_scanner_service.dart`. **Debe producir exactamente el mismo valor** para que el relink funcione con datos creados por Flutter (test con un archivo de referencia).
- Extensiones de video: `.mp4 .mkv .avi .mov .m4v .wmv .flv .webm .ts .mpg .mpeg` (sin distinguir mayúsculas).

## 6. Tablas nuevas `cl_*` en `cinemalove.sqlite`

```sql
CREATE TABLE cl_meta (clave TEXT PRIMARY KEY, valor TEXT NOT NULL);
-- schema_version, server_id (UUID estable de esta instancia como servidor)

CREATE TABLE cl_media_probe (            -- caché de ffprobe
  objetivo_tipo TEXT NOT NULL,           -- 'item' | 'extra'
  objetivo_id   TEXT NOT NULL,
  tamano_bytes  INTEGER NOT NULL,
  mtime_ms      INTEGER NOT NULL,        -- invalidar si cambia tamaño o mtime
  probe_json    TEXT NOT NULL,           -- salida resumida (ver 05-reproductor.md §3)
  creado_en     INTEGER NOT NULL,
  PRIMARY KEY (objetivo_tipo, objetivo_id)
);

CREATE TABLE cl_image_cache (
  url           TEXT PRIMARY KEY,        -- URL de TMDb
  ruta_relativa TEXT NOT NULL,           -- relativa a images/
  bytes         INTEGER NOT NULL,
  creado_en     INTEGER NOT NULL
);

CREATE TABLE cl_user_prefs (             -- preferencias por usuario remoto (NULL = admin local)
  usuario_id TEXT,                       -- sin FK a propósito para tolerar NULL en PK lógica
  clave      TEXT NOT NULL,
  valor      TEXT NOT NULL,
  UNIQUE (usuario_id, clave)
);
```

Índices nuevos permitidos (con prefijo `cl_idx_`) sobre tablas heredadas para el Catálogo (p. ej. `media_items(library_id, series_id, estado_archivo)`), porque SQLite/drift los ignora al migrar. Documentarlos en `migrations/`.

## 7. `cliente_datos.sqlite` (estado de este equipo como cliente)

Esquema heredado de `OldVersion/app/lib/data/database/client_tables.dart` (verificar su `user_version` y aplicar las mismas reglas de §2):

- `cliente_servidores`: `id` PK, `host`, `puerto` (UNIQUE host+puerto), `api_key` NULL, `tipo_credencial` (`apiKey`\|`sesion`, NULL = apiKey), `ultimo_usuario_sesion` NULL, `https` BOOL, `confiar_en_certificado` BOOL, `ultima_conexion`, `sesion_token_persistente` NULL, `expira_sesion_persistente` NULL.
- `cliente_progresos`, `cliente_proximas_a_ver`, `cliente_colecciones`, `cliente_coleccion_items`, `cliente_coleccion_progresos`: mismo patrón que las tablas del servidor, con `servidor_id` y `media_item_id` = id remoto. Solo se usan cuando la conexión es por **token (API key)**; con usuario/contraseña todo vive en el servidor.

Nuevo:
```sql
CREATE TABLE cl_cliente_servidor_extra (
  servidor_id      TEXT PRIMARY KEY,      -- = cliente_servidores.id
  nombre_servidor  TEXT,                  -- nombre amigable informado por /health
  server_uuid      TEXT,                  -- cl_meta.server_id del servidor remoto
  huella_cert_sha256 TEXT                 -- huella confiada (HTTPS autofirmado)
);
```

## 8. Caché de imágenes

- **Lectura de la caché heredada:** `images/imageCacheInfo.json` es el índice de `flutter_cache_manager` (`JsonCacheInfoRepository`). Contiene objetos con al menos la URL (`key`/`url`) y `relativePath` (nombre del archivo dentro de `images/`). Al arrancar, cargar ese índice en memoria (solo lectura) → mapa `url → archivo`. **Claude Code debe inspeccionar un archivo real** para confirmar los nombres de campos antes de implementar.
- **Nunca escribir** `imageCacheInfo.json` (Flutter lo gestiona).
- **Descargas nuevas:** `images/cl/<sha1(url)>.<ext>`, registradas en `cl_image_cache`.
- Resolución de una imagen: índice Flutter → `cl_image_cache` → descarga de TMDb (si hay red) → placeholder.
- Las imágenes se descargan **perezosamente** (al mostrarse), no durante la importación, igual que en Flutter.
- Se sirven por `GET /api/v1/image?src=<url-tmdb>` (ver `04`), tanto al renderer local como a clientes remotos. Solo se aceptan URLs de `https://image.tmdb.org/`.
- Tamaños TMDb: póster `w500`, backdrop `w1280`, perfil `w185`, still de episodio `w300`. Los clientes pueden pedir `&size=w342` para grillas: el servidor reescribe el segmento de tamaño de la URL y lo cachea aparte.

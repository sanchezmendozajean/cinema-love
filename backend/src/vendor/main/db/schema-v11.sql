-- Esquema heredado de drift v11 (volcado de una base real creada por Flutter). No modificar.
CREATE TABLE "libraries" ("id" TEXT NOT NULL, "nombre" TEXT NOT NULL, "tipo" TEXT NOT NULL, "ruta_actual" TEXT NOT NULL, "creado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), PRIMARY KEY ("id"));

CREATE TABLE "media_items" ("id" TEXT NOT NULL, "library_id" TEXT NOT NULL REFERENCES libraries (id) ON DELETE CASCADE, "ruta_relativa" TEXT NOT NULL, "hash_respaldo" TEXT NULL, "estado_archivo" TEXT NOT NULL, "tmdb_id" TEXT NULL, "titulo" TEXT NULL, "titulo_original" TEXT NULL, "anio" INTEGER NULL, "sinopsis" TEXT NULL, "poster_url" TEXT NULL, "backdrop_url" TEXT NULL, "duracion_segundos" INTEGER NULL, "generos" TEXT NULL, "creado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), "actualizado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), "reparto" TEXT NULL, "idioma_original" TEXT NULL, "estudio" TEXT NULL, "equipo" TEXT NULL, "series_id" TEXT NULL REFERENCES series (id) ON DELETE CASCADE, "numero_temporada" INTEGER NULL, "numero_episodio" INTEGER NULL, PRIMARY KEY ("id"));

CREATE TABLE "usuarios" ("id" TEXT NOT NULL, "nombre_usuario" TEXT NOT NULL UNIQUE, "password_hash" TEXT NOT NULL, "rol" TEXT NOT NULL, "creado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), PRIMARY KEY ("id"));

CREATE TABLE "watch_progresses" ("id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "media_item_id" TEXT NOT NULL REFERENCES media_items (id) ON DELETE CASCADE, "usuario_id" TEXT NULL REFERENCES usuarios (id) ON DELETE CASCADE, "posicion_segundos" INTEGER NOT NULL DEFAULT 0, "estado" TEXT NOT NULL, "ultima_reproduccion" INTEGER NULL, "pista_audio_id" TEXT NULL, "pista_subtitulo_id" TEXT NULL);

CREATE TABLE "api_keys" ("id" TEXT NOT NULL, "valor" TEXT NOT NULL UNIQUE, "creado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), "activa" INTEGER NOT NULL DEFAULT 1 CHECK ("activa" IN (0, 1)), PRIMARY KEY ("id"));

CREATE TABLE "app_settings" ("clave" TEXT NOT NULL, "valor" TEXT NOT NULL, PRIMARY KEY ("clave"));

CREATE TABLE "proximas_a_ver" ("id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "media_item_id" TEXT NOT NULL REFERENCES media_items (id) ON DELETE CASCADE, "usuario_id" TEXT NULL REFERENCES usuarios (id) ON DELETE CASCADE, "agregado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)));

CREATE TABLE "extras" ("id" TEXT NOT NULL, "media_item_id" TEXT NOT NULL REFERENCES media_items (id) ON DELETE CASCADE, "ruta_relativa" TEXT NOT NULL, "hash_respaldo" TEXT NULL, "estado_archivo" TEXT NOT NULL, "titulo" TEXT NULL, "creado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), PRIMARY KEY ("id"));

CREATE TABLE "collections" ("id" TEXT NOT NULL, "nombre" TEXT NOT NULL, "quitar_al_verse" INTEGER NOT NULL DEFAULT 0 CHECK ("quitar_al_verse" IN (0, 1)), "usuario_id" TEXT NULL REFERENCES usuarios (id) ON DELETE CASCADE, "creado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), PRIMARY KEY ("id"));

CREATE TABLE "collection_items" ("id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "collection_id" TEXT NOT NULL REFERENCES collections (id) ON DELETE CASCADE, "media_item_id" TEXT NOT NULL REFERENCES media_items (id) ON DELETE CASCADE, "agregado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), UNIQUE ("collection_id", "media_item_id"));

CREATE TABLE "collection_watch_progresses" ("id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, "collection_id" TEXT NOT NULL REFERENCES collections (id) ON DELETE CASCADE, "media_item_id" TEXT NOT NULL REFERENCES media_items (id) ON DELETE CASCADE, "usuario_id" TEXT NULL REFERENCES usuarios (id) ON DELETE CASCADE, "posicion_segundos" INTEGER NOT NULL DEFAULT 0, "estado" TEXT NOT NULL, "ultima_reproduccion" INTEGER NULL, "pista_audio_id" TEXT NULL, "pista_subtitulo_id" TEXT NULL);

CREATE TABLE "series" ("id" TEXT NOT NULL, "library_id" TEXT NOT NULL REFERENCES libraries (id) ON DELETE CASCADE, "ruta_relativa" TEXT NOT NULL, "estado_archivo" TEXT NOT NULL, "tmdb_id" TEXT NULL, "titulo" TEXT NULL, "titulo_original" TEXT NULL, "idioma_original" TEXT NULL, "anio" INTEGER NULL, "sinopsis" TEXT NULL, "estudio" TEXT NULL, "poster_url" TEXT NULL, "backdrop_url" TEXT NULL, "generos" TEXT NULL, "reparto" TEXT NULL, "equipo" TEXT NULL, "creado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), "actualizado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), PRIMARY KEY ("id"));

CREATE TABLE "seasons" ("id" TEXT NOT NULL, "series_id" TEXT NOT NULL REFERENCES series (id) ON DELETE CASCADE, "numero_temporada" INTEGER NOT NULL, "tmdb_id" TEXT NULL, "titulo" TEXT NULL, "sinopsis" TEXT NULL, "poster_url" TEXT NULL, "creado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), "actualizado_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), "reparto" TEXT NULL, "equipo" TEXT NULL, PRIMARY KEY ("id"), UNIQUE ("series_id", "numero_temporada"));

CREATE TABLE "sesiones" ("token" TEXT NOT NULL, "usuario_id" TEXT NOT NULL REFERENCES usuarios (id) ON DELETE CASCADE, "creada_en" INTEGER NOT NULL DEFAULT (CAST(strftime('%s', CURRENT_TIMESTAMP) AS INTEGER)), "expira_en" INTEGER NOT NULL, PRIMARY KEY ("token"));

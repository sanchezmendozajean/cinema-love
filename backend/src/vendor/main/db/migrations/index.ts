import type Database from 'better-sqlite3'

/**
 * Migraciones propias de Electron: solo tablas/índices con prefijo `cl_`. Nunca tocan tablas,
 * columnas ni `user_version` del esquema heredado (specs/02 §2). La versión vive en cl_meta.
 */
export interface Migracion {
  version: number
  sql: string
}

export const MIGRACIONES: Migracion[] = [
  {
    version: 1,
    sql: `
CREATE TABLE IF NOT EXISTS cl_meta (clave TEXT PRIMARY KEY, valor TEXT NOT NULL);

CREATE TABLE IF NOT EXISTS cl_media_probe (
  objetivo_tipo TEXT NOT NULL,
  objetivo_id   TEXT NOT NULL,
  tamano_bytes  INTEGER NOT NULL,
  mtime_ms      INTEGER NOT NULL,
  probe_json    TEXT NOT NULL,
  creado_en     INTEGER NOT NULL,
  PRIMARY KEY (objetivo_tipo, objetivo_id)
);

CREATE TABLE IF NOT EXISTS cl_image_cache (
  url           TEXT PRIMARY KEY,
  ruta_relativa TEXT NOT NULL,
  bytes         INTEGER NOT NULL,
  creado_en     INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS cl_user_prefs (
  usuario_id TEXT,
  clave      TEXT NOT NULL,
  valor      TEXT NOT NULL,
  UNIQUE (usuario_id, clave)
);

-- Índices para el Catálogo (SQLite/drift los ignoran al migrar).
CREATE INDEX IF NOT EXISTS cl_idx_media_items_catalogo ON media_items (library_id, series_id, estado_archivo);
CREATE INDEX IF NOT EXISTS cl_idx_watch_progresses_item ON watch_progresses (media_item_id, usuario_id);
CREATE INDEX IF NOT EXISTS cl_idx_proximas_item ON proximas_a_ver (media_item_id, usuario_id);
`
  },
  {
    version: 2,
    sql: `
-- Episodios por serie (estadísticas del catálogo, listados de episodios).
CREATE INDEX IF NOT EXISTS cl_idx_media_items_serie ON media_items (series_id, estado_archivo, numero_temporada, numero_episodio);
CREATE INDEX IF NOT EXISTS cl_idx_series_catalogo ON series (library_id, estado_archivo);
`
  }
]

export function versionClDeEsquema(db: Database.Database): number {
  const existe = db.prepare("SELECT 1 FROM sqlite_master WHERE name = 'cl_meta'").get()
  if (!existe) return 0
  const fila = db.prepare("SELECT valor FROM cl_meta WHERE clave = 'schema_version'").get() as
    | { valor: string }
    | undefined
  return fila ? Number(fila.valor) : 0
}

export function aplicarMigraciones(db: Database.Database): void {
  const actual = versionClDeEsquema(db)
  for (const m of MIGRACIONES.filter((x) => x.version > actual)) {
    db.transaction(() => {
      db.exec(m.sql)
      db.prepare("INSERT OR REPLACE INTO cl_meta (clave, valor) VALUES ('schema_version', ?)").run(
        String(m.version)
      )
      db.prepare("INSERT OR IGNORE INTO cl_meta (clave, valor) VALUES ('server_id', ?)").run(
        crypto.randomUUID()
      )
    })()
  }
}

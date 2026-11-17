/**
 * Ids de pista compatibles con mpv (specs/05 §3): la versión Flutter guardó `pista_audio_id` / `pista_subtitulo_id`
 * con la numeración de mpv. Los subtítulos internos van 1-based por tipo y mpv numera los externos DESPUÉS de los
 * internos, así que `ext-k` ↔ `(internos + k)`. Valores especiales: `auto` y `no`.
 */

/** Id de la API (`"2"`, `"ext-1"`) → id que se guarda en la base (el de mpv). */
export function idSubtituloAMpv(id: string, nInternos: number): string {
  const m = /^ext-(\d+)$/.exec(id)
  return m ? String(nInternos + Number(m[1])) : id
}

/** Id guardado (de mpv) → id de la API; los que superan a los internos son externos. */
export function idSubtituloDesdeMpv(guardado: string | null | undefined, nInternos: number): string | null {
  if (!guardado || guardado === 'auto') return null
  if (guardado === 'no') return 'no'
  const n = Number(guardado)
  if (!Number.isInteger(n) || n < 1) return null
  return n > nInternos ? `ext-${n - nInternos}` : String(n)
}

# 06 · Bibliotecas, escaneo y metadata

Comportamiento heredado de Flutter (`OldVersion/docs/ARQUITECTURA.md` §4–5 y `OldVersion/app/lib/data/services/`). Portar la **lógica**, no reinventarla: los resultados deben ser idénticos sobre los mismos archivos, para que los datos creados por una versión sigan siendo válidos en la otra.

## 1. Biblioteca

`nombre`, `tipo` (Películas | Series), `ruta_actual` (carpeta local o de red: `D:\Peliculas`, `\\NAS\video\Series`, `/mnt/nas/peliculas`). El `id` (UUID) es estable de por vida.

## 2. Convención de carpetas — Películas

- **Una carpeta por película en el primer nivel**: `Biblioteca/El Padrino (1972)/…`.
- Dentro: el video **directamente** en esa carpeta es el oficial. Si hay varios, el más pesado; el resto son **extras**. Si no hay ninguno directo, se toma el más pesado de todo el árbol (fallback).
- Cualquier otro video más anidado (`extras/`, `Featurettes/`, …) es un **Extra**: sin metadata TMDb, título derivado **solo del nombre de archivo**.

## 3. Convención de carpetas — Series

- Una carpeta por serie en el primer nivel: `Nombre` o `Nombre (Año)` → fila en `series`.
- Cualquier video dentro es un episodio (`media_items` con `series_id`).
- Temporadas: subcarpetas `Season 1`, `Season 01`, `Temporada 1`, `Temporada 01` (sin distinguir mayúsculas). Sin subcarpeta → temporada 1, salvo que el nombre del archivo traiga `SxxEyy`.
- Número de episodio: si **todos** los archivos de la temporada traen patrón (`S01E02`, `1x02`, `Episodio 02`, `Capítulo 02`) sin duplicados, se usan; si no, se numeran 1..N por orden natural del nombre de archivo.
- Referencia exacta: `episode_parser.dart`, `filename_parser.dart`, `library_scanner_service.dart`. Portar sus expresiones regulares y **sus tests** (escribir tests con los mismos casos).

## 4. Escaneo y reconciliación

Escanear = job cancelable con progreso (`X de Y`), con hashing en workers.

Para cada archivo encontrado:
1. Buscar `MediaItem` existente de esa biblioteca por `ruta_relativa`.
2. Si no hay, por `hash_respaldo` (el archivo se movió/renombró dentro de la biblioteca).
3. Si coincide: actualizar ruta, conservar metadata, progreso, colecciones, ajustes manuales. Estado `ok`.
4. Si no coincide nada: nuevo ítem (candidato a metadata).
5. Ítems existentes no encontrados: `estado_archivo = noEncontrado` — **nunca** se borran solos. No aparecen en Catálogo/Inicio, sí en administración.

### Cambio de ruta de una biblioteca
Al guardar una `ruta_actual` nueva: re-escanear y abrir la **pantalla de reconciliación** con tres secciones (solo las que tengan contenido):
- **Coincidencias por contenido** (reconciliados por hash): informativo.
- **Huérfanos** (no encontrados): por fila, **Volver a vincular…** (elegir archivo dentro de la biblioteca) o **Eliminar**; además, "Eliminar todos los huérfanos" con confirmación que muestra la cantidad exacta.
- **Nuevos**: conteo + botón "Buscar metadatos de los nuevos".

### Pestaña "Archivo" (editar ítem, solo Modo Local)
Muestra por separado carpeta de la biblioteca / carpeta de la película / archivo. Acciones:
- **Cambiar archivo…**: relink manual, valida que esté dentro de la biblioteca. Se aplica al guardar.
- **Homologar nombre** (solo películas): renombra carpeta y archivo a `Título (Año)/Título (Año).ext` con los valores actuales del formulario; chequeo de colisión previo, rollback de la carpeta si falla el archivo, actualiza `ruta_relativa` de sus extras. Se aplica al instante (confirmación previa mostrando nombre actual → nombre nuevo).

## 5. Metadata (TMDb)

- API key de TMDb del usuario (Ajustes → Metadatos). Validar con una request.
- **Idioma**: `es-MX`. Si título o sinopsis no tienen traducción, completar **campo por campo** con `en-US`.
- Datos por película: título, título original, idioma original, año, sinopsis, estudio, póster, backdrop, duración, géneros, reparto principal (con foto), equipo (dirección, guion, producción). Serie: lo mismo + por temporada `tv/{id}/season/{n}` (una request por temporada con episodios: nombre, sinopsis, still, duración).
- **Asistente de metadatos** (por biblioteca), jobs cancelables con resumen final (resueltos / ambiguos / sin coincidencia):
  - **Buscar faltantes**: ítems sin `tmdb_id`.
  - **Volver a buscar todo**: también los que ya tienen; la búsqueda se arma siempre desde el nombre de archivo/carpeta, nunca desde el título guardado (sirve para corregir errores en masa).
- **Coincidencia automática** solo si hay **una única** coincidencia exacta de título (localizado **u** original, contra todos los resultados) y, si el nombre trae año, coincide el año. Si el nombre trae año y ningún resultado lo tiene, **no** asignar: queda para revisión manual. Ver `tmdb_match.dart` (`elegirMejorCandidato`).
- **Búsqueda manual** desde la ficha: buscador con resultados de TMDb (póster, título, año, sinopsis corta). El año de referencia inicial sale del nombre de archivo, no del año guardado.
- **Edición manual** de cualquier campo (pestaña "Metadatos").
- Rate limiting: respetar los límites de TMDb (cola con concurrencia 4 y reintento con backoff en 429).
- Todo queda cacheado en SQLite: la app muestra todo sin internet una vez importado. Imágenes: perezosas (ver `02` §8).

## 6. Duración real

Si `duracion_segundos` falta (sin TMDb o episodio sin dato), usar la de ffprobe cuando se haga el primer `playback-info`, y guardarla (escritura en columna heredada, permitida).

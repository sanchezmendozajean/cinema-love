# 07 · Requisitos funcionales por área

Cada requisito tiene un id (`RF-xx`) para referenciarlo en commits y tests. "Admin" = solo Modo Local.

## Navegación general
- **RF-01** Barra lateral con: **Inicio**, **Catálogo**, **Bibliotecas**, **Colecciones**, **Próximas a ver**, y abajo **Ajustes**. En Modo Cliente, "Bibliotecas" muestra las del servidor sin acciones de administración.
- **RF-02** Búsqueda global: `Ctrl+K` o `/` abre una paleta de búsqueda (películas, series, episodios, personas del catálogo, colecciones). Enter en una consulta sin selección → Catálogo con `q`.
- **RF-03** Historial: botones Atrás/Adelante en la barra de título y `Alt+←/→`; los botones laterales del mouse también.
- **RF-04** Debajo de 900 px de ancho la barra lateral se colapsa a íconos.
- **RF-05** Todo modal tiene botón **X** visible y **no** se cierra al hacer clic afuera ni con arrastre; solo con X, Cancelar, Esc o una acción. (Regla heredada.)
- **RF-06** Toda acción destructiva pide confirmación explícita con el detalle de lo que se borra.

## Inicio
- **RF-10** Hero con el ítem `destacado` (backdrop a ancho completo, logo/título, año · duración · géneros, sinopsis corta, botones **Reproducir/Continuar** y **Más información**).
- **RF-11** Filas en este orden: **Continuar viendo** (progreso global entre 0 % y 95 %, por última reproducción; tarjetas apaisadas con barra de progreso; episodios muestran serie + `T1 · E3`), **Próximas a ver**, **Agregados recientemente** (últimos 20), **Vistos recientemente** (últimos 20). Filas vacías se ocultan.
- **RF-12** Las filas horizontales se desplazan con rueda del mouse (rueda vertical → scroll horizontal cuando el puntero está sobre la fila), con flechas de navegación al hacer hover y *snap* por tarjeta.
- **RF-13** Estado vacío sin bibliotecas: ilustración + "Agrega tu primera biblioteca" (Admin) o "Este servidor aún no tiene contenido" (Cliente).

## Catálogo (nuevo — una única lista con todo)
- **RF-20** Grilla de pósters con **todas** las películas y series de todas las bibliotecas (sin episodios ni huérfanos).
- **RF-21** **Búsqueda** por texto en vivo (debounce 250 ms), insensible a mayúsculas y acentos, sobre título y título original.
- **RF-22** **Filtro por género**: selección múltiple con conteos (facetas). OR entre géneros.
- **RF-23** **Filtro por biblioteca**: selección múltiple con conteos. OR entre bibliotecas.
- **RF-24** Filtros adicionales: Tipo (Todo / Películas / Series), Año (rango con slider o décadas), Estado (No vistas / En progreso / Vistas — se oculta si el principal no tiene progreso agregado).
- **RF-25** Orden: Título A–Z, Año (reciente primero), Agregado recientemente, Reproducido recientemente.
- **RF-26** Filtros distintos se combinan con AND. Los filtros activos se muestran como *chips* removibles y "Limpiar filtros" aparece solo si hay alguno. Contador: "**431** títulos" o "**12** de 431 títulos".
- **RF-27** Los filtros se reflejan en la URL interna (volver atrás restaura filtros y scroll).
- **RF-28** Densidad de grilla ajustable (pequeña / media / grande), recordada por equipo.
- **RF-29** Scroll infinito con virtualización (bibliotecas de miles de títulos sin degradar); páginas de 60.
- **RF-30** Entrar a una biblioteca desde "Bibliotecas" abre el Catálogo con esa biblioteca prefiltrada + una cabecera de biblioteca (nombre, tipo, conteo, y en Admin: Escanear, Metadatos, Editar).
- **RF-31** Sin resultados: mensaje con la consulta y botón "Limpiar filtros".

## Tarjeta de póster
- **RF-35** Póster 2:3, título y año debajo. Hover: elevación + borde de acento + botón play central + menú "⋯". Indicadores: barra de progreso (en curso), check (visto), contador de episodios no vistos (series), ícono de "sin metadatos" si falta TMDb.
- **RF-36** El menú "⋯" es el mismo en tarjetas y en la ficha: Reproducir, Marcar visto/no visto, Agregar/Quitar de Próximas a ver, Agregar a colección…, y en Admin: Editar metadatos, Buscar metadatos. Abierto desde dentro de una colección, "marcar visto" afecta el progreso de esa colección.
- **RF-37** Sin póster: placeholder elegante con el título tipográfico sobre un degradado derivado del nombre.

## Ficha de película
- **RF-40** Backdrop a pantalla completa con degradado; póster; título; título original si difiere; año · duración · clasificación de idioma; géneros como chips (clic → Catálogo filtrado por ese género); estudio.
- **RF-41** Acción principal: **Reproducir** o **Continuar (1 h 12 min restantes)** + "Empezar de nuevo". Secundarias: Próximas a ver (toggle), Agregar a colección, Marcar visto, menú ⋯.
- **RF-42** Sinopsis; **Reparto** (carrusel de fotos circulares con nombre y personaje; clic → ficha de persona); **Equipo** (Dirección, Guion, Producción).
- **RF-43** **Extras** (si hay): fila de tarjetas 16:9 reproducibles.
- **RF-44** **Detalles técnicos** (plegable): resolución, HDR, códecs, pistas de audio y subtítulos con idioma, tamaño, y modo de reproducción previsto (Direct/Remux/Transcode). En Admin también la ruta del archivo.
- **RF-45** "Está en las colecciones: …" con enlaces.

## Ficha de serie / temporada / episodio
- **RF-50** Serie: mismo hero que película; botón **Continuar T2 · E4** (siguiente episodio no visto) o **Reproducir T1 · E1**; grilla de **temporadas** (pósters 2:3 con progreso por temporada); reparto y equipo de la serie.
- **RF-51** Temporada: lista de episodios con still 16:9, número, título, duración, sinopsis inline, estado visto, botón reproducir superpuesto en el still y menú ⋯. Los episodios **no** tienen ficha propia. Acción "Marcar temporada como vista".
- **RF-52** Selector de temporada en la cabecera para saltar entre temporadas sin volver.

## Persona
- **RF-55** Foto, nombre, biografía (TMDb, es-MX con fallback), y **"En tu colección"**: películas y series del catálogo donde participa (funciona también en Modo Cliente vía `/people/<id>/in-catalog`).

## Colecciones y Próximas a ver
- **RF-60** Colecciones: grilla de tarjetas con mosaico de hasta 4 pósters, nombre y conteo. Crear, renombrar, eliminar (confirmación), opción **"Quitar elementos al terminar de verlos"**.
- **RF-61** Detalle de colección: secciones "Continuar viendo" (progreso propio de la colección) y "En esta colección" (grilla paginada). Reordenar no es requisito v1.
- **RF-62** Progreso **aislado por colección**: reproducir desde la colección continúa el progreso de esa colección; "marcar visto" ahí solo afecta a esa colección; si `quitar_al_verse`, el ítem sale al llegar a visto dentro de ella.
- **RF-63** Próximas a ver: grilla; usa el progreso **global**; un ítem sale solo al marcarse visto.

## Reproductor
- **RF-70** Ver `05-reproductor.md` completo.

## Bibliotecas (Admin)
- **RF-80** Lista de bibliotecas como tarjetas anchas (collage de backdrops, nombre, tipo, conteo, ruta, último escaneo).
- **RF-81** Agregar/Editar: nombre, tipo (no editable si ya tiene ítems), carpeta (selector nativo). Cambiar carpeta → reconciliación (`06` §4).
- **RF-82** Escanear (job con progreso en un panel de tareas global, no modal bloqueante) y resumen final (nuevos, actualizados, no encontrados).
- **RF-83** Asistente de metadatos (buscar faltantes / volver a buscar todo / eliminar huérfanos) con progreso, cancelación y resumen; lista de "ambiguos" para resolver uno a uno con el buscador manual.
- **RF-84** Eliminar biblioteca: confirmación escribiendo el nombre; aclarar que **no** borra archivos del disco, solo la catalogación y el progreso.

## Ajustes
- **RF-90** Secciones: **General** (idioma de la UI fijo en español v1, inicio con el sistema opcional), **Reproducción** (subtítulos: tamaño/posición; aceleración por hardware; calidad máxima de transcodificación; volumen por defecto), **Metadatos** (API key TMDb, enmascarada, botón probar), **Servidor** (`03` §4), **Usuarios**, **Seguridad** (token), **Conexión** (conectar a otro CinemaLove / desconectar / olvidar servidor), **Datos** (carpeta de datos actual, abrir en el explorador, "Reconfigurar…", tamaño de la caché de imágenes y limpiar temporales), **Acerca de** (versión, licencias de terceros incl. ffmpeg).
- **RF-91** En Modo Cliente solo se ven: Reproducción, Conexión, Acerca de.

## Tareas en segundo plano
- **RF-95** Indicador en la barra de título cuando hay jobs (escaneos, metadatos): clic abre un panel con progreso, cancelar y resultado. Al terminar, toast con resumen.

## Errores y conexión
- **RF-98** Modo Cliente: si el servidor deja de responder, banner superior "Se perdió la conexión con *Sala de estar* · Reintentando…" con reintento exponencial; la UI sigue mostrando lo ya cargado.
- **RF-99** Sesión vencida (401 con token de sesión): diálogo para reingresar la contraseña sin perder la vista actual.

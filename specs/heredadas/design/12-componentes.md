# 12 · Componentes

Inventario de componentes reutilizables. Claude Design los entrega como una hoja de componentes con sus variantes y estados; Claude Code los implementa en `renderer/src/components/` con los mismos nombres.

| Componente | Variantes | Estados / notas |
|---|---|---|
| `TitleBar` | Local, Local+Compartiendo, Cliente | Translúcida sobre héroe; chip de modo; indicador de tareas |
| `Sidebar` | expandida (232) / colapsada (72) | Ítem activo, hover, foco |
| `ModeChip` | Local · Compartiendo · Cliente · Desconectado | Punto de color: gris / verde / violeta / rojo |
| `Button` | primary (acento), secondary (superficie + borde), ghost, danger, icon | default, hover, active, focus, disabled, loading (spinner que reemplaza el ícono). Tamaños 32 / 40 / 48 (48 solo en héroe) |
| `PlayButton` | "Reproducir", "Continuar · quedan X", "Volver a ver" | Barra de progreso integrada opcional debajo |
| `IconToggle` | bookmark (Próximas a ver), check (visto) | off / on (relleno + acento) con micro-animación |
| `PosterCard` | película, serie (etiqueta SERIE), temporada; densidad S/M/L | hover (escala, borde acento, play central, ⋯), foco, progreso, visto, sin metadatos, sin imagen, skeleton |
| `LandscapeCard` | continuar viendo, extra, episodio en fila | barra de progreso, subtítulo `T1 · E3`, skeleton |
| `EpisodeRow` | normal, siguiente, visto, no encontrado | still con play superpuesto, sinopsis expandible |
| `PersonChip` | círculo 96 con nombre y rol | sin foto: iniciales sobre degradado |
| `CollectionCard` | mosaico 2×2, 1 póster, vacía; especial "Próximas a ver" | |
| `LibraryCard` | películas, series; "agregar" punteada | escaneando (barra fina animada) |
| `Hero` | Inicio (rotativo), Ficha | con/sin backdrop (fallback: degradado de acento + póster desenfocado) |
| `Row` | fila horizontal con título y flechas | flechas solo al hover; fade en los bordes |
| `Grid` | densidades S/M/L | virtualizada |
| `SearchField` | página, paleta | con atajo visible (`Ctrl K`), limpiar (×) |
| `FilterDropdown` | multiselección con conteos; rango de años; selección única (orden, tipo) | abierto/cerrado, con selección (muestra "Géneros · 2") |
| `FilterChip` | removible | |
| `SegmentedControl` | 2–3 opciones (Películas/Series, HTTP/HTTPS, Credenciales/Usar token) | |
| `TextField` | normal, contraseña/secreto (con ojo), solo lectura con copiar, ruta con "Elegir…" | error, éxito, disabled |
| `Switch`, `Checkbox`, `Slider` (simple y rango) | | |
| `Dialog` | S (420), M (560), L (720), pantalla completa | **Siempre con X**; no se cierra al hacer clic afuera; pie con acciones alineadas a la derecha |
| `ConfirmDialog` | normal, destructivo, destructivo con texto a escribir | |
| `Menu` (⋯) | acciones de ítem (ver RF-36) | separadores, ítems deshabilitados con tooltip, ítem destructivo |
| `Toast` | éxito, info, aviso, error | con acción opcional; autocierre 5 s |
| `Banner` | conexión perdida, sin metadatos, solo lectura | |
| `ProgressBar` | lineal fina (tarjetas), lineal con texto (jobs), indeterminada | |
| `JobItem` | en curso, completado, cancelado, error | |
| `EmptyState` | ilustración lineal + título + texto + CTA | ilustraciones propias (claqueta, rollo de película, butaca), trazo 1.5 px en `--text-faint` |
| `Skeleton` | rectángulo, póster, texto (líneas) | shimmer sutil |
| `QrCode` | sobre tarjeta blanca redondeada | para la URL del servidor |
| `Fingerprint` | huella SHA-256 agrupada en monoespaciada | botón copiar |
| **Reproductor** | | |
| `PlayerControls` | visible/oculto | degradados superior e inferior |
| `Timeline` | con buffer, hover con tiempo, arrastrando | altura 4 → 8 px al hover |
| `VolumeControl` | 0–200 % | la zona 100–200 % marcada con un tono más cálido |
| `Osd` | volumen (esquina), central (play/pausa/salto acumulado) | escala grande |
| `TrackMenu` | audio, subtítulos | activo con check; aviso "requiere conversión" |
| `PlaybackInfoPanel` | Direct (verde), Remux (ámbar), Transcode (ámbar) | |
| `NextEpisodeCard` | con cuenta regresiva circular | cancelar |

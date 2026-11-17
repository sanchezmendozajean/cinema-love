# 10 · Sistema de diseño

**Concepto: "sala oscura".** Fondo casi negro (no negro puro) para que pósters y backdrops sean protagonistas; un acento carmesí cálido —el "love" de CinemaLove— usado con moderación; tipografía con un toque editorial en los títulos, como un programa de cine. Elegante, moderno, sin ruido.

La app es **solo tema oscuro** (es una app para ver cine). No hay tema claro en v1.

## 1. Color

Base heredada de la versión Flutter (el usuario la aprobó; el rojo del modo Local y el violeta del modo Cliente **no se cambian**).

| Token | Valor | Uso |
|---|---|---|
| `--bg` | `#0E0D11` | Fondo de la app |
| `--surface` | `#19181E` | Tarjetas, barra lateral, paneles |
| `--surface-raised` | `#221F28` | Diálogos, menús, popovers |
| `--surface-hover` | `#2A2631` | Hover de filas e ítems de menú |
| `--border` | `#2E2A35` | Bordes de tarjetas e inputs |
| `--border-strong` | `#3D3846` | Divisores con énfasis |
| `--text` | `#F5F3F7` | Texto principal |
| `--text-muted` | `#A7A2B0` | Texto secundario, metadatos |
| `--text-faint` | `#6E6878` | Placeholders, deshabilitado |
| `--accent` | `#E23F52` (Local) / `#9519AE` (Cliente) | Acción primaria, foco, progreso, selección |
| `--accent-soft` | `#F2707F` (Local) / `#BA6ACA` (Cliente) | Hover del acento, textos de acento sobre fondo oscuro |
| `--accent-tint` | acento al 14 % de opacidad | Fondos de chips/estados seleccionados |
| `--success` | `#4CAF7D` | Visto, servidor activo |
| `--warning` | `#E0A63A` | Transcodificando, avisos |
| `--danger` | `#E5484D` | Acciones destructivas (distinto del acento para no confundir "primario" con "peligro") |
| `--scrim` | `rgba(14,13,17,.72)` | Fondo detrás de modales |

El acento se define con `[data-mode="local"]` / `[data-mode="client"]` sobre `<html>`. **Diseñar cada pantalla clave en ambos modos** (al menos Inicio y Ficha).

### Degradados de imagen
- Backdrop de héroe: degradado inferior `--bg` 0 % → 100 % en el 45 % inferior, y lateral izquierdo `--bg` 85 % → 0 % en el 55 % izquierdo (el texto se lee sobre el lado izquierdo).
- Opcional: un "ambient glow" muy sutil tomando el color dominante del backdrop, difuminado detrás del héroe (máx. 18 % de opacidad).

## 2. Tipografía

Fuentes empaquetadas localmente (licencia OFL):

| Rol | Fuente | Notas |
|---|---|---|
| Display (títulos de películas en héroe/ficha, encabezados de página) | **Fraunces** (variable, opsz alto, peso 600) | El toque editorial/cinéfilo |
| UI y texto | **Inter** (variable) | Números tabulares (`font-feature-settings: "tnum"`) en tiempos y contadores |

Escala (px, line-height):

| Token | Tamaño | Peso | Uso |
|---|---|---|---|
| `display-xl` | 56 / 60 | Fraunces 600 | Título en héroe de Inicio y ficha |
| `display-l` | 40 / 46 | Fraunces 600 | Encabezado de página (Catálogo, Colecciones) |
| `title-l` | 24 / 30 | Inter 600 | Títulos de sección ("Continuar viendo") |
| `title-m` | 18 / 24 | Inter 600 | Títulos de diálogo, nombres en tarjetas grandes |
| `body` | 15 / 22 | Inter 400 | Sinopsis, texto general |
| `body-s` | 13 / 18 | Inter 400/500 | Metadatos, títulos bajo póster |
| `caption` | 12 / 16 | Inter 500, tracking +2 % | Etiquetas, chips, contadores |
| `overline` | 11 / 14 | Inter 600, mayúsculas, tracking +8 % | Rótulos ("EN TU COLECCIÓN") |

## 3. Espaciado, radios, sombras

- Escala de espaciado (px): 4, 8, 12, 16, 20, 24, 32, 40, 56, 72.
- Margen lateral de página: 40 px (≥1280), 24 px (<1280), 16 px (compacto).
- Separación entre secciones verticales: 40 px. Entre tarjetas en grilla/fila: 16 px (20 en densidad grande).
- Radios: `--r-sm` 8 (chips, inputs pequeños), `--r-md` 12 (botones, inputs, pósters), `--r-lg` 16 (tarjetas grandes), `--r-xl` 20 (diálogos), `full` (avatares, pills).
- Sombras (sobre fondo oscuro se usan con moderación):
  - `--shadow-card-hover`: `0 12px 32px rgba(0,0,0,.55)` + `0 0 0 1px var(--accent)` (borde de acento).
  - `--shadow-pop`: `0 16px 48px rgba(0,0,0,.6)` para menús y diálogos.

## 4. Imágenes

| Tipo | Proporción | Tamaño de referencia |
|---|---|---|
| Póster (película, serie, temporada) | 2:3 | 160×240 en grilla media; 120 / 200 de ancho en densidad pequeña / grande |
| Backdrop / héroe | 16:9, a sangre | ancho completo, alto 62–70 vh en Inicio, 56 vh en ficha |
| Tarjeta apaisada ("Continuar viendo", extras, episodios) | 16:9 | 300×169 |
| Foto de persona | 1:1 circular (reparto) / 2:3 (ficha de persona) | 96 px círculo |
| Mosaico de colección | 2×2 pósters dentro de una tarjeta 4:3 | |

- Carga: placeholder con color de `--surface` y un shimmer sutil; aparición con fade de 200 ms. Nunca saltos de layout (reservar la proporción).
- Sin imagen: degradado determinístico a partir del título + título en Fraunces centrado.

## 5. Iconografía

**Material Symbols Rounded** (variable: relleno 0, peso 400, grado 0, tamaño óptico 24). Un único set en toda la app, incluido el reproductor. Relleno 1 para estados activos (p. ej. `bookmark` relleno = en Próximas a ver). Tamaños: 20 (UI densa), 24 (estándar), 40 (OSD del reproductor), 56 (play central).

Íconos clave: `home`, `grid_view` (Catálogo), `video_library` (Bibliotecas), `collections_bookmark` (Colecciones), `bookmark` (Próximas a ver), `settings`, `search`, `play_arrow`, `pause`, `replay_10`, `forward_10`, `volume_up`, `audiotrack`, `subtitles`, `fullscreen`, `info`, `more_horiz`, `check_circle`, `cast` / `lan` (servidor), `wifi_tethering` (compartiendo), `link_off` (desconectar), `close`.

## 6. Movimiento

- Duraciones: 120 ms (hover), 200 ms (aparición de elementos), 320 ms (transición de página), 450 ms (héroe).
- Curva: `cubic-bezier(.2,.8,.2,1)` (salida suave).
- Hover de tarjeta: escala 1.04 + sombra + borde de acento, el resto de la fila no se mueve.
- Transición de página: fade + 8 px de desplazamiento vertical. Del póster a la ficha: *shared element* del póster si es viable.
- Respetar `prefers-reduced-motion` (sin escalas ni desplazamientos, solo fades).

## 7. Accesibilidad y teclado

- Contraste de texto ≥ 4.5:1 (el `--text-muted` sobre `--bg` cumple).
- Foco visible siempre: anillo de 2 px `--accent-soft` con 2 px de separación.
- Las grillas y filas se navegan con flechas; Enter abre; `P` reproduce el ítem enfocado.
- Objetivos de clic ≥ 32 px en escritorio.

## 8. Voz y textos

Español neutro, cálido y breve. Tuteo ("Agrega tu primera biblioteca"). Sin jerga técnica en la UI general (en "Detalles técnicos" sí). Ejemplos:
- Vacío: "Aún no hay nada por aquí".
- Error de red: "No pudimos conectar con *Sala de estar*. Reintentando…"
- Confirmación: "¿Eliminar la colección *Clásicos de terror*? Las películas no se borran, solo la lista."

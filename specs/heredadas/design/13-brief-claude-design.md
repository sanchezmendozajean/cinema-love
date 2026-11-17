# 13 · Brief para Claude Design

## Prompt (copiar y pegar en Claude Design, adjuntando esta carpeta `design/` y `../07-funcionalidades.md`)

> Diseña las vistas de **CinemaLove**, una aplicación de escritorio (Electron, Windows y Linux) para catalogar y reproducir una colección personal de películas y series, que además puede compartir su catálogo en la red de la casa o conectarse al catálogo de otro equipo.
>
> **Estética**: elegante y moderna, "sala de cine oscura". Pósters y backdrops son los protagonistas; la interfaz se retira. Referencia de estructura: Jellyfin / Plex / Apple TV (héroes con backdrop, filas de pósters, fichas ricas), pero más refinada y editorial: títulos en serif (Fraunces), UI en Inter, mucho aire, acento carmesí usado con moderación. Solo tema oscuro.
>
> Sigue al pie de la letra los tokens de `10-sistema-de-diseno.md`, construye los componentes de `12-componentes.md` y diseña todas las pantallas de `11-pantallas.md` con sus estados (cargando, vacío, error) y la variante **Modo Cliente** (acento violeta `#9519AE` en lugar del rojo `#E23F52`).
>
> Prioridad: 1) Catálogo (la vista con **todos** los títulos, búsqueda y filtros por género y por biblioteca), 2) Inicio, 3) Ficha de película, 4) Reproductor, 5) Ficha de serie y temporada, 6) Asistente de bienvenida (elegir entre "Mi colección en este equipo" y "Conectarme a otro CinemaLove"), 7) Colecciones y Próximas a ver, 8) Ajustes (sobre todo Servidor), 9) Diálogos de administración.
>
> Usa contenido realista en español (películas y series reales con títulos de Latinoamérica, sinopsis cortas, años, duraciones). Lienzo 1440×900 y verificación a 1024×640. Interfaz en español neutro con tuteo.
>
> Reglas de interacción que deben verse en el diseño: todo diálogo tiene un botón **X** visible; las tarjetas muestran progreso, visto y un menú ⋯; los controles del reproductor usan íconos Material Symbols Rounded; los subtítulos van con contorno, sin recuadro de fondo.

## Entregables esperados

1. **Hoja de estilos / tokens** (colores, tipografía, espaciado, radios, sombras) coincidente con `10-sistema-de-diseno.md`. Si propones ajustes, márcalos como propuesta.
2. **Hoja de componentes** con variantes y estados (`12-componentes.md`).
3. **Prototipos HTML/React** navegables de las pantallas P0–P14, preferentemente con Tailwind sobre variables CSS (`--bg`, `--accent`, …) para que Claude Code los traslade casi directo.
4. Una nota por pantalla con decisiones de diseño no obvias (para Claude Code).

Guardar las exportaciones en `specs/design/prototipos/` (una carpeta por pantalla, p. ej. `P03-catalogo/`).

## Lo que NO hace falta diseñar

Tema claro, versión móvil/Android (llegará después; sí cuidar la variante compacta a 1024×640), páginas web públicas, logos de terceros. Para las imágenes usar marcadores (degradados, fotos libres o pósters inventados), no pósters oficiales; en la app real llegan de TMDb.

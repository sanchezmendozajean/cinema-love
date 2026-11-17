# 11 · Pantallas

Lienzo de referencia: **1440×900** (escritorio). Verificar también **1024×640** (mínimo de la ventana). Requisitos funcionales completos en `../07-funcionalidades.md` (ids `RF-xx`). Usar datos realistas de cine (títulos, años, sinopsis en español) — nada de "Lorem ipsum".

Cada pantalla debe incluir sus estados: **cargando** (skeletons con la forma real), **vacío**, **error** y, cuando aplique, **Modo Cliente** (acento violeta, sin acciones de administración).

---

## P0 · Shell (marco de la app)

```
┌─[≡]─CinemaLove──[←][→]───────[ 🔍 Buscar  Ctrl K ]──────[⟳ 1 tarea][● Compartiendo]─[–][□][×]┐
│ ┌────────┐ ┌──────────────────────────────────────────────────────────────────────────────┐ │
│ │ ⌂ Inicio│ │                                                                              │ │
│ │ ▦ Catál.│ │                         CONTENIDO DE LA PÁGINA                               │ │
│ │ ▤ Biblio│ │                                                                              │ │
│ │ ❏ Colec.│ │                                                                              │ │
│ │ 🔖 Próx.│ │                                                                              │ │
│ │         │ │                                                                              │ │
│ │ ⚙ Ajust.│ │                                                                              │ │
│ └────────┘ └──────────────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```
- **Barra de título** (40 px, arrastrable): logo/wordmark pequeño, atrás/adelante, campo de búsqueda (abre la paleta), indicador de tareas en segundo plano, **chip de modo** ("Local" · "Compartiendo en 192.168.0.115:8080" con punto verde · "Conectado a Sala de estar" con punto violeta), botones de ventana.
- **Barra lateral** (232 px; 72 px colapsada bajo 900 px de ancho): ítems con ícono + etiqueta; activo con barra vertical de acento a la izquierda y fondo `--accent-tint`. Debajo de "Colecciones" pueden listarse hasta 5 colecciones recientes en texto pequeño.
- En las páginas con héroe, el contenido pasa **por debajo** de la barra de título (barra translúcida con *blur*), para que el backdrop llegue hasta arriba.

## P1 · Asistente de bienvenida

Ventana sin barra lateral. Fondo: collage animado muy lento y oscurecido de backdrops genéricos (o un degradado de acento sutil si no hay imágenes). Tarjeta central de 640 px con indicador de pasos.

1. **Bienvenida / elegir modo**: logo grande, "Bienvenido a CinemaLove", dos tarjetas seleccionables lado a lado (ícono grande, título, descripción — textos en `../03-modos-de-ejecucion.md` §2).
2. **A1 Carpeta de datos**: ruta sugerida en un campo de solo lectura + "Elegir carpeta…"; variante con **colección existente detectada** (tarjeta de éxito con conteos y mini-mosaico de pósters); enlace "Usar los datos de mi versión anterior…".
3. **A2 TMDb**: campo enmascarado con ojo, botón "Probar", estado validado ✓, enlace "¿Cómo obtengo una clave?", botón "Omitir por ahora".
4. **A3 Primera biblioteca**: nombre, tipo (segmentado Películas/Series con íconos), carpeta. "Omitir".
5. **B1 Elegir servidor**: lista "En tu red" con animación de búsqueda (radar sutil), filas con nombre, dirección, candado; separador "o" y campo de URL manual.
6. **B2 Certificado** (solo HTTPS autofirmado): huella SHA-256 en monoespaciada agrupada de a 4, explicación breve, casilla "Confío en este certificado".
7. **B3 Credencial**: segmentado **Credenciales** / **Usar token**; campos; interruptor "Recordar este servidor y conectarme automáticamente"; botón **Conectar** con estado de carga; errores inline diferenciados.
8. **Listo**: transición a Inicio.

Estados de arranque fuera del asistente (misma estética): "Conectando a *Sala de estar*…" (spinner), "No se pudo conectar" (Reintentar / Olvidar este servidor), "Tu sesión venció" (campo de contraseña), "No encontramos la carpeta de datos" (Reintentar / Elegir otra / Reconfigurar).

## P2 · Inicio

```
┌──────────────────────────── HÉROE (backdrop 66vh) ────────────────────────────┐
│                                                                                │
│  CONTINUAR VIENDO                                                              │
│  El Padrino                       (Fraunces 56)                                │
│  1972 · 2 h 55 min · Drama, Crimen                                             │
│  Don Vito Corleone, jefe de una de las cinco familias…  (2 líneas)             │
│  [▶ Continuar · quedan 1 h 12 min]  [ⓘ Más información]                       │
│  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  │
└────────────────────────────────────────────────────────────────────────────────┘
 Continuar viendo                                                        [‹][›]
 [16:9 ▬▬▬──] [16:9 ▬────] [16:9 ▬▬▬▬─] [16:9 …]
 Próximas a ver
 [2:3] [2:3] [2:3] [2:3] [2:3] [2:3] [2:3] [2:3]
 Agregados recientemente
 [grilla 2:3 …]
 Vistos recientemente
 [2:3] [2:3] …
```
- El héroe puede rotar entre 3–5 destacados (puntos indicadores, pausa al hover, sin autoavance si `reduced-motion`).
- Variante sin progreso: rótulo "AGREGADO RECIENTEMENTE" y botón "Reproducir".
- Vacío (sin bibliotecas): ilustración lineal + CTA "Agregar biblioteca" (Local) / mensaje (Cliente).

## P3 · Catálogo (vista nueva y protagonista)

```
 Catálogo                                                    (Fraunces 40)
 431 títulos
┌───────────────────────────────────────────────────────────────────────────────┐
│ [🔍 Buscar por título…                    ] [Tipo: Todo ▾] [Orden: Título A–Z ▾] [▦▦▦]│
│ [Géneros ▾] [Bibliotecas ▾] [Año ▾] [Estado ▾]                                 │
│ Filtros: (Drama ×) (Crimen ×) (Películas 4K ×)          Limpiar filtros        │
└───────────────────────────────────────────────────────────────────────────────┘
 [2:3][2:3][2:3][2:3][2:3][2:3][2:3][2:3]
 [2:3][2:3][2:3][2:3][2:3][2:3][2:3][2:3]
 … (scroll infinito)
```
- La barra de filtros es **sticky** al hacer scroll (se compacta a una línea con fondo `--bg` translúcido + blur).
- **Popover de Géneros**: lista con casillas y conteo a la derecha ("Drama 210"), buscador interno si hay más de 12, botones "Seleccionar todo / Ninguno". Igual para **Bibliotecas** (con ícono de tipo película/serie y conteo).
- **Año**: slider de rango doble + atajos por década ("70s", "80s"…).
- Cada tarjeta muestra una pequeña etiqueta "SERIE" en la esquina superior izquierda del póster cuando es serie.
- Estados: cargando (grilla de skeletons), sin resultados ("No encontramos nada para «padrino» con estos filtros" + Limpiar filtros), catálogo vacío.
- **Variante "Biblioteca"** (al entrar desde Bibliotecas): encima de la barra, cabecera de biblioteca con collage de backdrops difuminado, nombre, tipo, conteo y (Local) botones **Escanear**, **Metadatos ▾**, **Editar**. El filtro de biblioteca aparece preseleccionado como chip.
- Mostrar las 3 densidades de grilla.

## P4 · Bibliotecas

Grilla de tarjetas anchas (2–3 por fila): collage de 3 backdrops en franja superior, nombre (title-m), tipo con ícono, "380 películas · 12 sin metadatos", ruta (Local, texto faint, truncada al medio), "Escaneada hace 2 días". Hover → acciones (Escanear, Editar, ⋯). Tarjeta final punteada "＋ Agregar biblioteca" (Local). Clic → P3 variante Biblioteca.

## P5 · Ficha de película

```
┌──────────────────────── BACKDROP a sangre (56vh) ────────────────────────┐
│ ┌──────┐                                                                  │
│ │      │  El Padrino                                    (Fraunces 56)     │
│ │POSTER│  The Godfather · 1972 · 2 h 55 min · EN                          │
│ │ 2:3  │  (Drama) (Crimen)        Paramount Pictures                      │
│ │      │  [▶ Continuar · 1 h 12 min restantes] [↺] [🔖] [＋ Colección] [✓] [⋯]│
│ └──────┘  ━━━━━━━━━━━━━━━━━━──────────  (progreso)                          │
└───────────────────────────────────────────────────────────────────────────┘
 Sinopsis (máx. 70 caracteres por línea)
 Reparto        ( ◯ ) ( ◯ ) ( ◯ ) ( ◯ ) ( ◯ ) ( ◯ ) →
                Marlon Brando · Don Vito Corleone
 Equipo         Dirección: Francis Ford Coppola · Guion: Mario Puzo, F. F. Coppola …
 Extras         [16:9] [16:9]
 En tus colecciones:  Clásicos · Maratón Coppola
 ▸ Detalles técnicos   4K · HDR10 · HEVC · E-AC3 5.1 Latino, AAC Inglés · Subtítulos: Español, Inglés
                       Reproducción: Remux (audio no compatible)
```
- Estados: sin metadatos (póster placeholder, título del archivo, banner "Este título no tiene metadatos · Buscar en TMDb" en Local), archivo no encontrado (botón reproducir deshabilitado con tooltip), visto (check y "Volver a ver").

## P6 · Ficha de serie y temporada

- **Serie**: mismo héroe que P5; botón "Continuar T2 · E4 — *Nombre del episodio*"; info "3 temporadas · 26 episodios · 10 vistos"; sección **Temporadas** con pósters 2:3, "Temporada 1 · 10 episodios" y barra de progreso; reparto y equipo.
- **Temporada**: cabecera compacta (backdrop de la serie difuminado, póster de temporada pequeño, "Dark · Temporada 2", selector de temporada ▾, "Marcar temporada como vista"). Lista de episodios:

```
 [ still 16:9 ▶ ]  3. Pasado y presente                       52 min   ✓
                   Jonas busca respuestas mientras… (sinopsis en 2 líneas, expandible)   ⋯
```
  El episodio en curso muestra barra de progreso sobre el still y queda resaltado como "Siguiente".

## P7 · Persona

Foto 2:3 a la izquierda, nombre (Fraunces 40), "Actuación · 1924–2004", biografía con "Leer más", y **En tu colección**: grilla de pósters con el rol debajo ("como Don Vito Corleone").

## P8 · Colecciones

- **Lista**: encabezado "Colecciones" + botón "Nueva colección". Tarjeta especial fija "Próximas a ver" primero (ícono bookmark, conteo). Tarjetas 4:3 con mosaico 2×2 de pósters, nombre, "14 títulos", ícono si tiene "quitar al ver".
- **Detalle**: encabezado con mosaico grande difuminado, nombre editable inline (lápiz), conteo, menú (Renombrar, Opciones, Eliminar). Secciones "Continuar viendo" (16:9) y "En esta colección" (grilla). Vacío: "Agrega películas desde su menú ⋯ › Agregar a colección".

## P9 · Próximas a ver

Grilla de pósters con orden "Agregado recientemente"; vacío con explicación de cómo agregar ("Usa el ícono 🔖 en cualquier título").

## P10 · Reproductor

```
┌───────────────────────────────────────────────────────────────────────┐
│ ←  El Padrino                                         (degradado sup.) │
│                                                                (🔊 140%)│ ← OSD volumen
│                                                                         │
│                                ( ⏸ )                                    │ ← OSD central
│                                                                         │
│              Subtítulo con contorno, sin recuadro                       │
│ 1:42:10 ━━━━━━━━━━━━━━━━━━━━━━━━●━━━━━━━━━━━━━━━━━━━━━━━━━━━━ -1:12:50  │
│ [⏯] [⟲10] [⟳10] [🔊 ━━━●━━] 140%        [♪] [CC] [⏭] [ⓘ] [⛶]           │
└───────────────────────────────────────────────────────────────────────┘
```
Diseñar: controles visibles; controles ocultos (solo video); OSD de volumen; OSD de salto acumulado ("+20 s"); toast "Presiona Esc de nuevo para salir"; menú de **audio** (lista con idioma, códec, canales, check en la activa); menú de **subtítulos** (Desactivados, lista, aviso en los de imagen, "Configuración…"); modal pequeño de **configuración de subtítulos** (sliders de tamaño y posición, el video visible detrás); panel **ⓘ Info técnica** (modo Direct/Remux/Transcode con color: verde/ámbar/ámbar, resolución, códecs, bitrate); **buffering** (spinner sobre el último frame); **siguiente episodio** (tarjeta abajo a la derecha con cuenta regresiva); **fin de película**; **error** ("No se pudo reproducir este archivo" + detalle técnico plegable + Reintentar con conversión). La barra de tiempo muestra el tiempo al pasar el mouse (tooltip).

## P11 · Ajustes

Layout de dos columnas: índice de secciones a la izquierda (sticky), contenido a la derecha en tarjetas por grupo. Secciones en `../07-funcionalidades.md` RF-90. Diseñar en detalle:
- **Servidor**: tarjeta de estado grande (interruptor "Compartir mi colección en la red", estado con punto de color, nombre del servidor editable); protocolo segmentado HTTP/HTTPS; puerto; **direcciones para conectarse** con botón copiar y **código QR**; certificado (huella, regenerar); casillas mDNS y autoinicio; tabla "Reproduciendo ahora" (usuario, título, modo, dispositivo, botón detener).
- **Usuarios**: tabla (nombre, creado, sesiones activas) + "Nuevo usuario" (diálogo con usuario/contraseña/confirmación).
- **Seguridad**: token enmascarado con ojo, copiar, regenerar (confirmación).
- **Conexión**: en Local, botón "Conectarme a otro CinemaLove" (abre el mismo flujo B del asistente en un diálogo); en Cliente, tarjeta del servidor actual (nombre, dirección, tipo de credencial, usuario) con "Desconectar" y "Olvidar este servidor".
- **Reproducción**: vista previa estática de subtítulo sobre un fotograma con sliders.
- **Datos**: ruta, abrir carpeta, tamaño de caché, "Limpiar temporales", "Reconfigurar…" (con advertencia).

## P12 · Diálogos de administración (Local)

- **Agregar/Editar biblioteca**: nombre, tipo segmentado, carpeta con selector; aviso al cambiar la carpeta ("Se volverá a escanear; tu progreso y metadatos se conservan").
- **Reconciliación** (pantalla completa, no diálogo): tres secciones (`../06-bibliotecas-y-metadata.md` §4) con contadores grandes arriba.
- **Asistente de metadatos**: elegir acción → progreso (barra, "57 de 412", título actual con miniatura, Cancelar) → resumen (resueltos ✓, ambiguos ⚠ con lista para resolver, sin coincidencia ✕).
- **Buscar en TMDb**: campo de búsqueda + año, resultados en lista con póster, título, título original, año, sinopsis de 2 líneas; selección y "Asignar".
- **Editar metadatos**: pestañas **Metadatos** (campos, géneros como chips editables, póster/backdrop con vista previa) y **Archivo** (rutas desglosadas, "Cambiar archivo…", "Homologar nombre" con vista previa *antes → después*).
- **Confirmación destructiva**: ícono de advertencia, título claro, detalle de lo que se pierde y lo que **no** se pierde, botón rojo `--danger`; para eliminar biblioteca, campo para escribir el nombre.
- **Agregar a colección**: lista de colecciones con casillas (multi), "＋ Nueva colección" inline.

## P13 · Paleta de búsqueda (Ctrl+K)

Diálogo centrado arriba (640 px), campo grande, resultados agrupados (Películas, Series, Episodios, Personas, Colecciones) con miniatura, navegación con flechas, Enter abre, `Ctrl+Enter` reproduce. Pie: "Ver todos los resultados en Catálogo ↵".

## P14 · Panel de tareas y notificaciones

Popover desde el indicador de la barra de título: lista de jobs con barra de progreso, cancelar, resultados recientes. Toasts abajo a la derecha (éxito, aviso, error) con acción opcional.

---

## Checklist de entrega por pantalla
- [ ] Estado normal con datos realistas
- [ ] Cargando (skeleton)
- [ ] Vacío
- [ ] Error
- [ ] Modo Cliente (donde cambia algo)
- [ ] 1024×640
- [ ] Hover y foco de teclado en los elementos interactivos

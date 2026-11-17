# Prototipos de CinemaLove (Claude Design)

Exportación del lienzo de Claude Design. Una carpeta por pantalla según `11-pantallas.md`; los componentes y tokens siguen `10-sistema-de-diseno.md` y `12-componentes.md`.

## Cómo están armados

- Cada `.dc.html` es una mesa de trabajo de Claude Design (formato Design Component). Sirve como referencia de marcado y estilos; no es un componente de producción.
- En cada carpeta, el archivo principal (`Main`, `Inicio`, `Ficha`, `Reproductor`, …) contiene la pantalla completa con todos sus estados. Recibe props como `modo` (`local`/`cliente`), `estado`, `densidad` o `compacto`.
- Los demás archivos de la carpeta (`Catalogo-Cargando`, `Rep-Audio`, …) son envoltorios que importan el principal con otras props, uno por estado. Por eso cada estado vive junto a su pantalla.
- `cinemalove.css` reúne los tokens (`--bg`, `--accent`, …) y las clases base. El acento cambia con `data-mode="cliente"` (`#9519AE`). Las mesas lo enlazan como `./cinemalove.css`, así que para abrirlas fuera del lienzo hay que copiarlo junto a ellas.
- Íconos: Material Symbols Rounded. Fuentes: Fraunces, Inter y JetBrains Mono (vía Google Fonts en el prototipo; en la app van empaquetadas localmente).
- `canvas.json` guarda la disposición del lienzo.

## Notas por pantalla (decisiones no obvias)

### 00 · Tokens y componentes
- **Propuesta:** token `--ff-m` (JetBrains Mono) para huellas SHA-256, rutas y detalles técnicos.
- El texto blanco sobre `#E23F52` da ~4.1:1, por debajo de 4.5:1 para 14 px. Se mantuvo el rojo (no se cambia) con texto en peso 600; conviene validarlo.
- Las clases de `cinemalove.css` mapean 1:1 a los componentes de `12-componentes.md`: `.btn`, `.pc` (PosterCard), `.lc` (LandscapeCard), `.dd` (FilterDropdown), `.seg`, `.field`, `.dialog`, `.menu`, `.toast`, `.banner`, `.timeline`, etc.

### P01 · Asistente
- Los pasos del flujo B (servidor, certificado, credencial, conectando) ya usan el acento violeta para anticipar el Modo Cliente; el flujo A y la elección de modo usan rojo.
- El indicador de pasos cambia sus etiquetas según el flujo: Modo · Datos · TMDb · Biblioteca, o Modo · Servidor · Certificado · Credencial.

### P02 · Inicio
- La barra de título se vuelve casi transparente sobre el héroe, que sube hasta arriba.
- La barra de progreso del héroe va integrada en el botón Continuar (PlayButton), no a lo ancho del héroe.
- Las filas tienen un desvanecido a la derecha; las flechas solo se muestran en "Continuar viendo" para ilustrar el hover.
- El banner de conexión perdida (Cliente) flota centrado bajo la barra de título y no desplaza el contenido.

### P03 · Catálogo (interactivo)
- Tipo (Todo / Películas / Series) se resolvió como control segmentado en vez de lista desplegable: un clic en lugar de dos.
- La búsqueda ignora mayúsculas y tildes; géneros y bibliotecas usan OR internamente y AND entre filtros, con chips removibles.
- La variante desplazada compacta toda la barra en una línea con fondo translúcido y blur.
- El año combina un rango y atajos por década; en el chip activo se resume como "1970–1989".
- La etiqueta "SERIE" va en la esquina superior izquierda; los indicadores (visto, episodios sin ver, sin metadatos) van arriba a la derecha y se ocultan al hacer hover para dejar lugar al ⋯.

### P04 · Bibliotecas
- La tarjeta que está escaneando muestra una barra fina en el borde inferior del collage y el estado en color de acento.

### P05 · Ficha de película
- El rol visual: póster solapado sobre el borde inferior del backdrop, título en Fraunces 56.
- "Detalles técnicos" muestra en una línea el resumen y el modo previsto (Remux en ámbar) incluso plegado.
- Sin metadatos: backdrop de respaldo con degradado de acento y póster difuminado, título del archivo en monoespaciada y banner "Buscar en TMDb" (solo Local).
- El menú ⋯ agrupa las acciones de administración bajo el rótulo "Administración"; en Cliente esa sección no existe.

### P06 · Serie y temporada
- Episodio siguiente: fila resaltada con `--accent-tint`, chip "Siguiente" y barra de progreso sobre el still.
- Episodio no encontrado: still en gris, sin botón de reproducir; el mensaje cambia entre Local y Cliente.

### P08–P09 · Colecciones y Próximas a ver
- "Próximas a ver" aparece como tarjeta fija al inicio de Colecciones.
- El ícono `auto_delete` marca las colecciones con "Quitar elementos al terminar de verlos".

### P10 · Reproductor
- Volumen de 0 a 200 %: la zona 100–200 % se pinta en ámbar, tanto en el control como en el OSD.
- Subtítulos con contorno (`-webkit-text-stroke` + `paint-order`), sin recuadro de fondo. Suben cuando hay controles visibles.
- Al pasar el mouse por la barra de tiempo se ve una miniatura del fotograma con el tiempo.
- La configuración de subtítulos es un diálogo pequeño a la derecha, sin velo, para ver el video detrás.

### P11 · Ajustes
- El índice izquierdo es sticky; en Cliente solo muestra Reproducción, Conexión y Acerca de.
- El código QR es decorativo en el prototipo; en la app se genera a partir de la URL real.

### P12–P14 · Diálogos, búsqueda y tareas
- Todos los diálogos tienen X visible y el pie con acciones alineadas a la derecha.
- La confirmación destructiva separa "Se pierde" de "No se borra" y, para eliminar una biblioteca, pide escribir su nombre.
- El panel de tareas cuelga del indicador de la barra de título sin velo; los toasts se apilan abajo a la derecha.
- La reconciliación es una pantalla completa (no un diálogo) con tres contadores grandes arriba.

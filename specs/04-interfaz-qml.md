# 04 · Interfaz en QML

> Aplica a la opción A de `00` §4.1 (QML nativa). Si se elige la híbrida (web embebida), este documento se sustituye por la descripción del puente `QWebChannel` y la UI sigue siendo la de `electron-version/specs/design/`.

Las pantallas, componentes y el sistema de diseño **no cambian**: `electron-version/specs/design/10-sistema-de-diseno.md`, `11-pantallas.md` y `12-componentes.md` siguen siendo la fuente de verdad. Aquí solo se define cómo se traducen a Qt Quick.

## 1. Tokens y tema

- `qml/theme/Tokens.qml` es un **singleton** (`pragma Singleton`) con todos los tokens de `design/10`: colores, radios, espaciados, duraciones y curvas de animación, tamaños de tipografía. Es el único sitio donde viven (equivale a `tokens.css`).
- El tema por modo (identidad violeta en Modo Cliente, ver `electron-version/specs/03` §6) se resuelve con una propiedad `Tokens.modo` y *bindings*; ningún componente conoce colores sueltos.
- Fuentes: cargadas con `FontLoader` o `QFontDatabase::addApplicationFont` desde recursos. Iconos: Material Symbols Rounded como fuente de iconos; componente `Icono { nombre, tamano, relleno }`.
- Estilo base de `QtQuick.Controls`: **`Basic`**, con cada control personalizado (`background`, `contentItem`). No se usa el estilo nativo de la plataforma.

## 2. Equivalencias de componentes

| `design/12` / web | QML |
|---|---|
| Tarjeta de póster / apaisada | `PosterCard.qml`, `LandscapeCard.qml` (con `Image` asíncrona y *placeholder*) |
| Fila por categoría con scroll horizontal | `ListView { orientation: Qt.Horizontal }` con `snapMode` suave y botones de flecha |
| Grilla del Catálogo | `GridView` con delegado ligero y `cacheBuffer` ajustado (ver §4) |
| Diálogos (Radix) | `Popup`/`Dialog` propios con `modal: true`, botón **X** visible y `closePolicy: Popup.NoAutoClose` (o solo `CloseOnEscape`); nunca cierre al hacer clic afuera |
| Menús ⋯ | `Menu` personalizado |
| Selects, tooltips, toasts | `ComboBox`/`ToolTip` personalizados; `Toast` propio |
| Transiciones de página | `StackView` con `Transition` y curvas de `Tokens` |
| Hover de tarjetas | `HoverHandler` + `Behavior`/`NumberAnimation` |
| Scroll con barra | `ScrollBar` personalizada (fina, aparece al interactuar) |
| Backdrops grandes con degradado | `Image` + `ShaderEffect` o `LinearGradient` (`Qt5Compat.GraphicalEffects` no; usar `MultiEffect` de Qt 6.5+ o un `ShaderEffect` propio) |

## 3. Navegación y estado

- `StackView` raíz con `Shell.qml` (barra lateral/superior según `design/11`) y vistas intercambiables; el historial del botón atrás lo gestiona `NavigationController` (C++ o JS).
- El estado compartido (sesión, modo, preferencias, progreso en vivo) vive en objetos C++ registrados con `QML_ELEMENT`/`QML_SINGLETON`. QML solo declara; la lógica vive en C++.
- Las listas se alimentan de `QAbstractListModel` con carga paginada (`canFetchMore`/`fetchMore`), no de arrays JS grandes.

## 4. Rendimiento (criterio de aceptación: 60 fps con 2 000 pósters)

- **Delegados baratos**: un `Image` y un par de `Text`; nada de `Layout` anidados ni `ShaderEffect` por tarjeta. Sombras/hover con un solo recurso precomputado.
- **Imágenes** con un `QQuickImageProvider` asíncrono (`requestImageResponse`): decodificación en `QThreadPool`, escalado al tamaño de destino (`sourceSize`) y miniaturas `w342` para grillas; caché LRU en memoria y caché en disco compartida con `images/` (`electron-version/specs/02` §8).
- `GridView.cacheBuffer` moderado y `reuseItems: true` en `ListView`/`GridView`.
- Evitar `clip: true` anidado; evitar propiedades que fuercen *layer* innecesariamente.
- Medir con el *Qt Quick Scene Graph visualizer* (`QSG_VISUALIZE`) y el perfilador de QML en cada fase.

## 5. Accesibilidad y teclado

- Foco visible y navegación con flechas en grillas y filas (`KeyNavigation`, `focus`, `activeFocusOnTab`).
- Atajos globales con `Shortcut`; los del reproductor, solo activos dentro de `PlayerView` (`electron-version/specs/05` §5).
- `Accessible.name/role` en controles propios para lectores de pantalla.

## 6. Ventana y arranque sin parpadeos

- Mostrar la ventana con el fondo oscuro del tema desde el primer fotograma (sin destello blanco); un *splash* ligero hasta que `/health` responda.
- Persistencia de tamaño/posición como en `01` §5.
- Modo de renderizado de respaldo (`software`) accesible desde Ajustes si la GPU da problemas.

## 7. Pantallas

Se implementan todas las de `electron-version/specs/design/11` y las funciones RF-xx de `electron-version/specs/07`, en el mismo orden de prioridad que `electron-version/specs/20` (Fases 3, 5 y 6). La única pantalla que cambia en comportamiento es el **reproductor** (`02`).

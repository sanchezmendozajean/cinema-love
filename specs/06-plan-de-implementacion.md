# 06 · Plan de implementación

Cada fase termina con la app compilando, tests en verde y criterios de aceptación verificados. No avanzar con criterios pendientes sin avisar. **Regla de oro de los datos**: trabajar solo sobre copias en `.dev-data/`.

La Fase 0 es una **puerta de decisión**: si no pasa, no se continúa.

## Fase 0 — Spike de validación (puerta de decisión)
Prototipo mínimo, no el proyecto final.
- Ventana Qt Quick sin marco con OpenGL forzado y un `MpvItem` reproduciendo un archivo local.
- Controles QML superpuestos (play/pausa, barra de tiempo, volumen) y un subtítulo ASS.
- Seek, cambio de pista de audio y subtítulo, pantalla completa, redimensionado.
- Probar con la biblioteca real: MKV H.264/AC3, MKV HEVC 10-bit, AVI XviD, MKV con PGS, archivo en red (SMB) y un archivo por HTTP con `Range`.
- Reproducir una película de ≥ 2 h con pausas largas y varios saltos; dejar la app abierta horas para vigilar el fallo de ventana gris.
- Verificar en Windows y en Linux (X11 y Wayland).
- **Aceptación**: criterios 1 y 2 de `00` §5. Si falla el render con mpv, probar el plan B (Qt Multimedia/FFmpeg) y documentar el resultado en la memoria del proyecto. Decidir A/B de interfaz y A/B de backend.

## Fase 1 — Esqueleto y backend
- Proyecto Qt en la raíz de `qt6-version/` con CMake, presets, recursos, `Tokens.qml`, fuentes e iconos locales.
- Ventana sin marco, barra de título propia, instancia única, persistencia de ventana, modo gráfico de respaldo.
- Opción A: extraer el backend de Electron a un paquete ejecutable con Node plano; `BackendLauncher` (puerto, token, health, cierre, reintento).
- `ApiClient` y `getBootstrap` equivalentes; pantalla de arranque/error.
- **Aceptación**: `dist:win` y `dist:linux` producen un «hola mundo» que arranca el backend, responde `/health` y se cierra sin dejar procesos huérfanos.

## Fase 2 — Vistas de navegación
Shell, Inicio, Catálogo, Bibliotecas (solo lectura), fichas de película/serie/temporada/persona, Colecciones, Próximas a ver, tarjetas y menú ⋯ (`specs/heredadas/20` Fase 3).
- `ImageProvider` con caché; modelos paginados.
- **Aceptación**: RF-01…RF-63 salvo administración; 60 fps de scroll con 2 000 pósters en una máquina de referencia; estados de carga/vacío/error presentes; foco con teclado.

## Fase 3 — Reproductor
`02-reproductor-mpv.md` completo: controles, atajos, OSD, pistas, subtítulos, configuración de subtítulos, progreso y reanudación, siguiente episodio, pantalla final, info técnica, errores.
- **Aceptación**: matriz de archivos de `specs/heredadas/20` Fase 4 reproducida **sin sesiones ffmpeg**; seek, cambio de pista y reanudación en todos; progreso guardado aquí se reanuda en Flutter y en Electron, y viceversa (ids de pista de mpv); 3 reproducciones de ≥ 2 h consecutivas sin pausas de recuperación.

## Fase 4 — Administración
Bibliotecas CRUD, escáner (jobs/SSE), reconciliación, huérfanos, relink, homologar, TMDb (asistente masivo, edición manual) (`specs/heredadas/20` Fase 5). En opción A solo es UI sobre endpoints existentes.
- **Aceptación**: igual que `specs/heredadas/20` Fase 5.

## Fase 5 — Servidor público y Modo Cliente
Ajustes → Conexión, listener público, certificados, mDNS, usuarios, token, sesiones activas, QR; asistente de bienvenida; Modo Cliente (persistente y de sesión) con `cliente_datos.sqlite`, confianza por huella e identidad violeta (`specs/heredadas/20` Fase 6).
- Reproducción en cliente por `media/<id>/direct`; indicador de `paused-for-cache` y mensaje claro si la red no alcanza.
- **Aceptación**: dos equipos en la LAN con descubrimiento y reproducción; un cliente Flutter se conecta al servidor.

## Fase 6 — Pulido y empaquetado
Animaciones, accesibilidad, rendimiento de imágenes, logs (archivo rotatorio), «Acerca de» con licencias (`05` §4), instalación en máquina limpia, Linux en Ubuntu/Fedora en X11 y Wayland.
- **Aceptación**: checklist visual de `design/11`; builds autocontenidos; prueba de estabilidad de ≥ 8 h con la app abierta sin ventana en blanco.

## Decisión de continuidad
Al cerrar la Fase 3 comparar con la versión Electron en la biblioteca real (pausas de reproducción, estabilidad gráfica, consumo de memoria, esfuerzo restante) y decidir si esta alternativa pasa a ser la principal.

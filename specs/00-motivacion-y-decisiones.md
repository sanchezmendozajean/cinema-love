# 00 · Motivación y decisiones

## 1. Por qué una alternativa

La versión Electron reproduce con el `<video>` de Chromium. Lo que Chromium no entiende (MKV, AC3/DTS, algunos HEVC, AVI) obliga a pasar por ffmpeg en vivo (remux/transcode). Ese stream no es un archivo: el servidor conserva solo una ventana en memoria y Chromium puede reabrir la conexión en bytes ya descartados (HTTP 416). La app se recupera reiniciando la sesión, y el usuario ve una pausa y un retroceso de unos segundos.

Es un problema **recurrente** y estructural del modelo, no de un bug puntual. La versión Flutter no lo tenía porque usaba media_kit (libmpv): reproduce el archivo tal cual.

Esta alternativa recupera ese modelo de reproducción (libmpv) sin volver a Flutter, donde el usuario tuvo otros problemas (ventana gris tras un rato de uso). Es el mismo enfoque que **Jellyfin Media Player / Jellyfin Desktop**: Qt + mpv.

## 2. Qué se gana

- Reproducción directa de casi cualquier contenedor y códec, sin sesiones ffmpeg al vuelo: se acaban los 416, las pausas de recuperación y el retroceso al reiniciar un remux.
- HDR, audio DTS/TrueHD/E-AC3 y subtítulos ASS con estilos, sin recodificar.
- Aceleración por hardware (`hwdec`) nativa de mpv.
- Seek instantáneo sobre el archivo (en local, sin arranque de sesión).
- Cambiar de pista de audio o subtítulo sin reiniciar nada.

## 3. Qué se pierde o cuesta

- **Reescritura de la interfaz**: lo hecho en React no se reutiliza (salvo en la variante híbrida, §4.1).
- **Nuevo stack**: C++/QML, CMake, empaquetado de Qt y libmpv por plataforma.
- **Tokens y componentes**: se reimplementan en QML (grillas con miles de pósters exigen cuidado, ver `04`).
- **Menos ecosistema web**: no hay Radix, Tailwind ni Framer Motion; las animaciones y accesibilidad se resuelven con Qt Quick.
- **Riesgo técnico acotado**: la integración mpv + Qt Quick requiere OpenGL (ver `02` §2). Se valida primero con un *spike* (Fase 0).
- Dos bases de código mientras coexistan la versión Electron y esta.

## 4. Decisiones abiertas

### 4.1 Interfaz: QML nativa o web embebida

| | **A. QML nativa (propuesta)** | **B. Híbrida: web UI en QtWebEngine + mpv debajo** |
|---|---|---|
| Qué es | Toda la interfaz en QML | Se reutiliza la UI React; mpv se dibuja en una superficie nativa y la web va encima con fondo transparente (así lo hace Jellyfin Media Player) |
| Reutiliza lo hecho | No | Sí, casi toda la UI |
| Controles sobre el video | Sin problema | Delicado: depende de transparencia del webview y del orden de superficies |
| Peso y memoria | Bajo | Alto (Chromium otra vez, ahora dentro de Qt) |
| Problemas de renderizado | Controlables | Hereda los de Chromium/GPU |
| Riesgo | Esfuerzo de reescritura | Complejidad del puente web↔nativo y artefactos de composición |

**Recomendación:** A. La variante B conserva el esfuerzo ya hecho, pero mantiene Chromium en la ecuación y complica el reproductor, que es justo lo que se quiere simplificar. Si se elige B, `04-interfaz-qml.md` pasa a describir el puente (`QWebChannel`) en lugar de componentes QML; `02` y `05` aplican igual.

### 4.2 Backend: decidido, C++ integrado
Ver `03-backend-y-datos.md`. Se empezó con un sidecar Node y se portó por completo a C++ (18/11/2026) para que el proyecto sea autocontenido y sin proceso hijo.

## 5. Criterio de éxito

Se da por buena la alternativa si, con la biblioteca real del usuario:
1. Una película de ≥ 2 h en MKV (H.264, HEVC 10-bit, audio AC3/DTS) se reproduce de principio a fin **sin ninguna pausa de recuperación** ni retroceso, incluyendo pausas largas y saltos.
2. La interfaz no se queda en blanco ni congelada tras horas de uso (el fallo de la versión Flutter).
3. El catálogo se navega a 60 fps con 2 000 pósters.
4. Se conserva la compatibilidad de datos y API con Flutter y Electron.

Si el *spike* (Fase 0) falla en 1 o 2, se descarta la alternativa antes de invertir más.

# 00 · Visión y alcance

## Qué es

CinemaLove es una aplicación personal para catalogar y reproducir una colección de películas y series guardadas en disco local o en red. Se abre y funciona: sin cuentas, sin administrar un servidor tradicional. Inspiración visual: Jellyfin (pósters, backdrops grandes, filas por categoría), con más cuidado en el detalle — debe sentirse como un **catálogo curado**, no como una lista de archivos.

La misma aplicación cumple tres roles:

1. **Reproductor local** de una colección de archivos en este equipo.
2. **Servidor**: comparte su catálogo en la red para que otros equipos (u otra instancia de CinemaLove, o el futuro cliente Android) lo naveguen y reproduzcan por streaming.
3. **Cliente**: se conecta a otra instancia de CinemaLove que actúa como servidor.

## Plataformas

| Plataforma | Rol | Estado |
|---|---|---|
| Windows 10/11 x64 | Local + Servidor + Cliente | Esta versión |
| Linux x64 (AppImage / tar.gz) | Local + Servidor + Cliente | Esta versión |
| Android | Solo cliente | Más adelante, proyecto aparte. Consume `04-api-servidor.md` |
| macOS | — | Fuera de alcance |

## Qué cambia respecto a la versión Flutter

| Tema | Flutter (OldVersion) | Electron (esta versión) |
|---|---|---|
| Framework | Flutter/Dart | Electron + TypeScript + React |
| Reproducción | mpv (`media_kit`) | `<video>` + ffmpeg (direct play / remux / transcode) |
| Elegir modo | Asistente al primer arranque + cambio en Ajustes | Igual, rediseñado |
| Navegar todo | Por biblioteca | **Nuevo "Catálogo" unificado** con búsqueda y filtros por género y biblioteca |
| Clientes remotos | Pedían imágenes directo a TMDb | El servidor también sirve las imágenes en caché (los clientes funcionan sin internet) |
| Inicio en modo Cliente con usuario | "Continuar viendo" vacío (limitación) | Resuelto con endpoint agregado `/api/v1/home` |
| Descubrir servidores | URL manual | URL manual + descubrimiento automático en la LAN (mDNS) |
| Datos | `cinemalove.sqlite` + `images/` | **Los mismos archivos**, reutilizados |

## Glosario

- **Biblioteca**: carpeta del disco registrada en la app, de tipo *Películas* o *Series*. (Tabla `libraries`.)
- **Ítem / MediaItem**: un archivo de video: una película o un episodio. (Tabla `media_items`.)
- **Serie**: la ficha padre de una carpeta de serie; sus episodios son MediaItems. (Tabla `series`.)
- **Extra**: video bonus dentro de la carpeta de una película. (Tabla `extras`.)
- **Catálogo**: vista unificada de todas las películas y series de todas las bibliotecas.
- **Colección**: lista personal creada por el usuario, mezcla películas/series/episodios, con progreso aislado. (Tabla `collections`.)
- **Próximas a ver**: lista por defecto ("watchlist"). Se vacía sola al marcar visto.
- **Modo Local** (antes "App Principal"): la app trabaja sobre bibliotecas de este equipo, con acceso de administrador implícito.
- **Modo Cliente**: la app navega el catálogo de otra instancia por HTTP.
- **Servidor**: el servicio HTTP/HTTPS que una instancia en Modo Local puede activar para compartir su catálogo.
- **Direct play / Remux / Transcode**: las tres formas de reproducir (ver `05-reproductor.md`).

## Principios

- **Cero fricción**: abrir y disfrutar. Sin cuentas en uso local.
- **Autocontenida**: el build no requiere instalar Node, .NET, Python, ffmpeg ni codecs. Todo va empaquetado.
- **Nunca perder datos**: cambiar la ruta de una biblioteca no borra metadata ni progreso. Toda operación destructiva pide confirmación explícita y, si se puede, ofrece primero una alternativa no destructiva.
- **Offline**: todo funciona sin internet salvo la importación de metadata desde TMDb.
- **Pulido visual**: estados de carga, vacío y error diseñados; transiciones suaves; nada "funcional pero feo".

## Fuera de alcance (v1)

- Cliente Android (solo se diseña su contrato de API).
- Acceso desde fuera de la LAN (NAT, relays). Se puede exponer el puerto manualmente, pero no se asiste.
- Transcodificación por hardware obligatoria (se ofrece como opción si ffmpeg la detecta).
- Descarga de contenido para ver offline en el cliente.
- Sincronización de progreso entre el modo token (API key) y el modo usuario.

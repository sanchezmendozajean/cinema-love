# 03 · Modos de ejecución: Local, Servidor y Cliente

## 1. Resumen

```
                 ┌─────────────── Arranque ───────────────┐
                 │ ¿Existe config.yaml?                    │
                 └──────┬──────────────┬──────────────┬────┘
                   no   │      Local   │   Cliente     │ inválido
                        ▼              ▼   persistente ▼            ▼
              Asistente de      Abrir biblioteca   Reconectar      Pantalla de error
              bienvenida        local (Modo Local)  al servidor     (Reintentar /
                                (+ servidor si      (Modo Cliente)  Reconfigurar)
                                 autostart)
```

| Modo | Origen de datos | Permisos | Acento visual | Título |
|---|---|---|---|---|
| **Local** | `cinemalove.sqlite` vía listener interno | Administrador implícito, sin login | Rojo `#E23F52` | CinemaLove |
| **Local + Servidor activo** | Igual; además expone el catálogo en la red | Igual | Rojo + indicador "Compartiendo" | CinemaLove |
| **Cliente** | Servidor remoto por HTTP(S) | Solo navegar, reproducir, progreso, colecciones, Próximas a ver | Violeta `#9519AE` | CinemaLove Client |

## 2. Asistente de bienvenida (primer arranque)

Se muestra si no existe `config.yaml`, o tras "Reconfigurar". Pregunta lo esencial: **¿cómo vas a usar CinemaLove?**

### Paso 1 — Elegir modo
Dos tarjetas grandes:
- **"Mi colección en este equipo"** — "Organiza y reproduce películas y series guardadas en este equipo o en una unidad de red. También podrás compartirlas con otros dispositivos."
- **"Conectarme a otro CinemaLove"** — "Mira el catálogo de otro equipo de tu casa que ya tenga CinemaLove."

### Camino A — Colección local
1. **Carpeta de datos**: dónde guardar la base de datos e imágenes. Sugerencia por defecto: `<carpeta del ejecutable>/CinemaLoveData`. Botón "Elegir carpeta…".
   - Si la carpeta elegida ya contiene `cinemalove.sqlite` → **detectar y mostrar resumen**: "Encontramos una colección existente: 3 bibliotecas · 412 películas · 18 series. Se usará tal cual." (Compatibilidad con la versión Flutter.)
   - Atajo: botón secundario "Usar los datos de mi versión anterior…" que pide elegir el `config.yaml` de la instalación Flutter y toma su `database_directory`.
2. **TMDb** (opcional, se puede saltar): campo para la API key con enlace "¿Cómo obtengo una?" (abre el navegador). Validar con una request de prueba.
3. **Primera biblioteca** (opcional, se puede saltar): nombre, tipo (Películas/Series), carpeta. Al terminar ofrece "Escanear ahora".
4. Listo → escribir `config.yaml` (`database_directory`) y entrar a Inicio.

### Camino B — Conectarme a otro CinemaLove
1. **Elegir servidor**:
   - Lista "**En tu red**" alimentada por mDNS en vivo (nombre del servidor, host:puerto, candado si es HTTPS). Estado vacío: "Buscando servidores… Asegúrate de que el otro equipo tenga activado *Compartir en la red*".
   - "**Ingresar dirección manualmente**": un único campo de URL completa (`http://192.168.0.115:8080`; el esquema decide HTTP/HTTPS).
2. **Verificar**: `GET /api/v1/health` sin credencial. Errores distintos: "No hay respuesta en esa dirección" vs. "Hay algo en esa dirección pero no es un servidor CinemaLove".
   - Si es HTTPS con certificado autofirmado: mostrar la **huella SHA-256** y pedir "Confío en este certificado" (comparar con la que muestra el servidor en Ajustes → Servidor). Guardar la huella en `cl_cliente_servidor_extra`.
3. **Credencial**: selector segmentado **"Credenciales"** (usuario + contraseña) / **"Usar token"** (API key). `POST /api/v1/login` o `GET /api/v1/libraries` para validar.
4. **Recordar este servidor** (activado por defecto en el asistente): al abrir la app se reconecta sola (Modo Cliente persistente). Escribe `config.yaml` con `modo: cliente`.
5. Entrar a Inicio en Modo Cliente.

Reglas heredadas de Flutter:
- La contraseña **nunca** se guarda. En modo persistente se guarda el **token de sesión** (30 días). Al vencer → pantalla "Tu sesión venció" que pide la contraseña de nuevo.
- La API key sí se guarda (enmascarada en la UI).
- Si la reconexión automática falla (host inalcanzable) → pantalla "No se pudo conectar a *Sala de estar*" con **Reintentar**, **Olvidar este servidor** (borra `config.yaml` → asistente). **Nunca** caer a Modo Local en silencio.

## 3. Modo Local

- Abre `cinemalove.sqlite` (ver `02` §2 para las verificaciones), levanta el listener interno y muestra la UI completa con administración.
- Si `cl.server_autostart = true`, arranca también el servidor público.

### Cambiar de modo en caliente (desde Ajustes → Conexión)
- **"Conectarme a otro CinemaLove"** desde Modo Local: mismo formulario del asistente, conexión **solo para la sesión** (no toca `config.yaml`). La UI pasa a Modo Cliente (acento violeta, sin administración). El servidor público, si estaba activo, **sigue activo** (otros dispositivos siguen viendo esta colección) — mostrarlo en el indicador.
- **"Desconectar"** vuelve a Modo Local sin reiniciar. Si la conexión era persistente, borrar `config.yaml` solo si describe esa conexión.

## 4. Servidor (compartir en la red)

Disponible solo en Modo Local. Ajustes → Servidor:

- Interruptor **Compartir mi colección en la red** (iniciar/detener sin reiniciar).
- **Nombre del servidor** (por defecto el nombre del equipo, p. ej. "Sala de estar"), anunciado por mDNS y devuelto por `/health`.
- **Protocolo**: HTTP **o** HTTPS (exclusivo, como en Flutter; no se puede cambiar con el servidor corriendo).
- **Puerto** (por defecto 8080 HTTP / 8443 HTTPS; claves heredadas `server_http_port`/`server_https_port`). Validar que esté libre antes de guardar.
- **Direcciones para conectarse**: lista de IPs de la LAN con botón copiar y un **código QR** de la URL (pensado para el futuro cliente Android).
- **Certificado** (HTTPS): huella SHA-256 visible, "Regenerar certificado" (confirmación: los clientes tendrán que volver a confiar).
- **Anunciar en la red (mDNS)**: activado por defecto.
- **Iniciar automáticamente al abrir CinemaLove**.
- Panel "**Reproduciendo ahora**": sesiones activas de streaming (usuario/token, título, direct play vs transcodificando, dispositivo). Permite cortar una sesión.

Acceso:
- **Usuarios** (Ajustes → Usuarios): crear, cambiar contraseña, eliminar (cascada de sesiones).
- **Token de acceso** (Ajustes → Seguridad): generar/regenerar/copiar la API key.
- Usuario y token tienen permisos de **cliente**: navegar, reproducir, progreso, Próximas a ver, colecciones. Nunca administración.

### mDNS
- Tipo de servicio: `_cinemalove._tcp`, puerto del servidor.
- TXT: `v=1` (versión de API), `id=<cl_meta.server_id>`, `name=<nombre>`, `tls=0|1`.
- El cliente deduplica por `id` y prefiere IPv4.
- El firewall de Windows pedirá permiso la primera vez que se active el servidor; la UI debe anticiparlo con un aviso breve.

## 5. Modo Cliente — dónde vive cada dato

| Dato | Conectado con **Credenciales** (usuario) | Conectado con **token** (API key) |
|---|---|---|
| Catálogo, imágenes | Servidor | Servidor |
| Progreso global | Servidor (por usuario) | `cliente_datos.sqlite` local |
| Próximas a ver | Servidor | Local |
| Colecciones + su progreso | Servidor | Local |
| Preferencias de subtítulos | Local (este equipo) | Local |

Motivo: un token no identifica a una persona; mezclar progresos de varios usuarios del mismo token sería incorrecto. No hay fusión automática entre ambos mundos.

## 6. Identidad visual por modo

- El acento (`--accent`, `--accent-soft`) cambia en toda la UI: botones primarios, foco, barras de progreso, hover de tarjetas, indicadores. Implementar con un atributo `data-mode="local|client"` en `<html>` que redefine las variables.
- La barra de título muestra un **chip de modo**: "Local" / "Compartiendo · 192.168.0.115:8080" / "Conectado a *Sala de estar*".

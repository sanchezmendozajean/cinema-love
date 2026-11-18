#pragma once

class HttpServer;

/** Rutas de datos personales (progreso, Próximas a ver, colecciones, estado agregado): puerto de routes/usuario.ts. */
void registerUserRoutes(HttpServer &h);

#pragma once

class HttpServer;

/** Imágenes de TMDb con caché local (images/) y fichas de persona (biografía de TMDb): puerto de routes/imagen.ts y persona-service.ts. */
void registerImageRoutes(HttpServer &h);

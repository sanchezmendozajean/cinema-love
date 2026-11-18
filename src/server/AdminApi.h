#pragma once

class HttpServer;
class JobManager;

/** Administración (solo listener interno y principal admin): bibliotecas, trabajos, escáner, metadata, usuarios y ajustes. Puerto de routes/admin.ts. */
void registerAdminRoutes(HttpServer &h, JobManager &jobs);

#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>
#include <optional>

/**
 * Cliente HTTP(S) de los servidores remotos (Modo Cliente). Todo es BLOQUEANTE (bucle de eventos local): se usa desde hilos de trabajo.
 * Con `certPem` el certificado del servidor se exige IGUAL a ese (autofirmado confiado por huella); sin él, validación normal del sistema.
 */
namespace Remote {

struct Dest {
    QString host;
    int port = 0;
    bool https = false;
    QString certPem;   // certificado confiado (vacío = ninguno)
};

QString urlOf(const Dest &d);                           // http(s)://host:puerto
std::optional<Dest> parseUrl(const QString &text);      // acepta «http://192.168.0.115:8080», «https://sala.local» o «192.168.0.115:8080»

struct Response {
    int status = 0;                 // 0 = no hubo respuesta HTTP
    QByteArray body;
    bool certError = false;         // el fallo fue de certificado
    QString error;
};

using Headers = QList<QPair<QByteArray, QByteArray>>;

Response request(const Dest &d, const QString &method, const QString &pathAndQuery, const Headers &headers = {}, const QByteArray &body = {}, int timeoutMs = 10000);

struct JsonResponse {
    int status = 0;
    QJsonValue json;                // null si no era JSON
    bool certError = false;
    QString error;
};
/** GET/POST JSON. `body` no nulo = POST con ese cuerpo. */
JsonResponse requestJson(const Dest &d, const QString &pathAndQuery, const Headers &headers = {}, const QJsonValue *body = nullptr, int timeoutMs = 10000);

struct CertInfo {
    bool ok = false;
    QString pem;
    QString fingerprint;    // SHA-256 en hexadecimal con «:»
    bool valid = false;     // el sistema ya lo considera válido (firmado por una CA conocida)
};
/** Lee el certificado que presenta el servidor (sin confiar en él todavía). */
CertInfo readCertificate(const QString &host, int port, int timeoutMs = 5000);

QString normalizeFingerprint(const QString &h);
bool fingerprintMatches(const QString &a, const QString &b);

struct ProbeResult {
    // ok | certificado-no-confiable | certificado-cambiado | sin-respuesta | no-cinemalove
    QString type;
    Dest dest;
    QJsonObject health;
    QString fingerprint, pem;
};
/** Comprueba una dirección con GET /api/v1/health sin credencial. */
ProbeResult probe(const Dest &d);

}

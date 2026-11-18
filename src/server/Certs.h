#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * Certificado autofirmado propio para el servidor HTTPS (RSA 2048, SHA-256, 10 años, SAN con localhost, el nombre del equipo y las IP de la
 * LAN). Todo el cálculo (números grandes, firma, ASN.1) es código del proyecto: no hay biblioteca criptográfica externa. Los clientes no
 * validan por cadena de confianza sino por huella («Confío en este certificado»), así que los SAN son solo cortesía.
 */
namespace Certs {

struct Certificate {
    QByteArray certPem;      // -----BEGIN CERTIFICATE-----
    QByteArray keyPem;       // -----BEGIN RSA PRIVATE KEY----- (PKCS#1)
    QString fingerprint;     // SHA-256 del DER en hexadecimal con «:» (AB:CD:…)
    bool valid() const { return !certPem.isEmpty() && !keyPem.isEmpty(); }
};

/** Genera un certificado nuevo (unos segundos en una CPU lenta: la búsqueda de primos). */
Certificate generate(const QString &name, const QStringList &ips);

/** Huella SHA-256 de un certificado PEM; vacía si no se puede leer. */
QString fingerprintOf(const QByteArray &certPem);

/** Certificado persistido en `<datos>/certs/server.crt` y `server.key`: se reutiliza entre arranques para que los clientes no tengan que volver a confiar. */
class Storage
{
public:
    explicit Storage(const QString &dataFolder) : m_dir(dataFolder + QStringLiteral("/certs")) {}
    bool exists() const;
    Certificate load() const;                                   // vacío si falta o está corrupto
    void save(const Certificate &c) const;
    Certificate loadOrGenerate(const QString &name, const QStringList &ips) const;
    Certificate regenerate(const QString &name, const QStringList &ips) const;
private:
    QString m_dir;
};

}

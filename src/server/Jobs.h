#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>

/**
 * Trabajos de larga duración (escaneos, importación de metadata): port de jobs/job-manager.ts. Un solo trabajo corriendo por biblioteca; los de
 * «todas las bibliotecas» no conviven con otro del mismo tipo. Cada trabajo corre en un hilo de trabajo; los cambios y las líneas de registro se
 * emiten como señales en el hilo del servidor (para el stream SSE).
 */
class JobManager : public QObject
{
    Q_OBJECT
public:
    struct Context {
        std::function<bool()> aborted;
        std::function<void(const QString &)> line;
        std::function<void(int, int)> progress;
    };
    using Fn = std::function<QJsonValue(Context &)>;

    struct Running : std::runtime_error {
        explicit Running(const QString &jobId_) : std::runtime_error("Ya hay un trabajo en curso para esta biblioteca."), jobId(jobId_) {}
        QString jobId;
    };

    explicit JobManager(QObject *parent = nullptr) : QObject(parent) {}

    /** Inicia un trabajo (lanza Running si choca con otro) y devuelve su resumen. `libraryId` nulo = todas las bibliotecas. */
    QJsonObject start(const QString &type, const QVariant &libraryId, const QString &title, Fn fn);
    QJsonObject summary(const QString &id, bool withLines = false) const;
    QJsonArray list() const;
    bool exists(const QString &id) const;
    bool cancel(const QString &id);
    /** ¿Hay un trabajo corriendo de esa biblioteca (o de todas)? */
    bool busyFor(const QString &libraryId) const;
    bool anyRunning() const;
    void cancelAll();

signals:
    void changed(const QJsonObject &summary);
    void line(const QJsonObject &entry);   // {id, texto}

private:
    struct Job {
        QString id, type, title, state = QStringLiteral("corriendo");
        QVariant libraryId;
        bool hasProgress = false;
        int done = 0, total = 0;
        QStringList lines;
        QJsonValue result;
        QString error;
        qint64 createdAt = 0, finishedAt = 0;
        std::atomic<bool> abort{false};
    };
    QJsonObject summaryOf(const Job &j, bool withLines) const;

    mutable QMutex m_mutex;
    QList<std::shared_ptr<Job>> m_jobs;
};

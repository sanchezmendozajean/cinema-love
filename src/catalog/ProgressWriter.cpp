#include "ProgressWriter.h"

#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace ProgressWriter {

bool isDevCopy(const QString &dbPath)
{
    return QDir::fromNativeSeparators(dbPath).contains(QStringLiteral("/.dev-data/"));
}

bool save(const QString &dbPath, const QString &id, qint64 pos, const QString &state, const QString &audioId, const QString &subId)
{
    if (!isDevCopy(dbPath)) {
        qWarning("ProgressWriter: se omite guardar; la base no es una copia de .dev-data (%s)", qPrintable(dbPath));
        return false;
    }
    const bool watched = state == QLatin1String("visto");
    bool ok = false;
    const QString conn = QStringLiteral("save-") + QUuid::createUuid().toString(QUuid::Id128);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (db.open() && db.transaction()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT id, pista_audio_id, pista_subtitulo_id FROM watch_progresses WHERE media_item_id = ? AND usuario_id IS NULL"));
            q.addBindValue(id);
            ok = q.exec();
            const bool exists = ok && q.next();
            const qint64 rowId = exists ? q.value(0).toLongLong() : 0;
            const QVariant keepAudio = exists ? q.value(1) : QVariant(), keepSub = exists ? q.value(2) : QVariant();
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            const QVariant a = audioId.isEmpty() ? keepAudio : QVariant(audioId);
            const QVariant sb = subId.isEmpty() ? keepSub : QVariant(subId);
            QSqlQuery w(db);
            if (ok) {
                if (exists) {
                    w.prepare(QStringLiteral("UPDATE watch_progresses SET posicion_segundos = ?, estado = ?, ultima_reproduccion = ?, pista_audio_id = ?, pista_subtitulo_id = ? WHERE id = ?"));
                    w.addBindValue(pos); w.addBindValue(state); w.addBindValue(now); w.addBindValue(a); w.addBindValue(sb); w.addBindValue(rowId);
                } else {
                    w.prepare(QStringLiteral("INSERT INTO watch_progresses (media_item_id, usuario_id, posicion_segundos, estado, ultima_reproduccion, pista_audio_id, pista_subtitulo_id) VALUES (?, NULL, ?, ?, ?, ?, ?)"));
                    w.addBindValue(id); w.addBindValue(pos); w.addBindValue(state); w.addBindValue(now); w.addBindValue(a); w.addBindValue(sb);
                }
                ok = w.exec();
            }
            if (ok && watched) {
                QSqlQuery d(db);
                d.prepare(QStringLiteral("DELETE FROM proximas_a_ver WHERE media_item_id = ? AND usuario_id IS NULL"));
                d.addBindValue(id);
                ok = d.exec();
            }
            if (ok) ok = db.commit(); else db.rollback();
        }
        if (!ok) qWarning("ProgressWriter: no se pudo guardar el progreso: %s", qPrintable(db.lastError().text()));
        db.close();
    }
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

} // namespace ProgressWriter

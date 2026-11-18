#include "Jobs.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>

namespace {
constexpr int kMaxLines = 2000;
constexpr int kMaxHistory = 30;
}

QJsonObject JobManager::summaryOf(const Job &j, bool withLines) const
{
    QJsonObject o{{"id", j.id}, {"tipo", j.type}, {"bibliotecaId", j.libraryId.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(j.libraryId.toString())}, {"titulo", j.title},
                  {"estado", j.state}, {"progreso", j.hasProgress ? QJsonValue(QJsonObject{{"hechos", j.done}, {"total", j.total}}) : QJsonValue(QJsonValue::Null)},
                  {"resultado", j.result.isUndefined() ? QJsonValue(QJsonValue::Null) : j.result}, {"error", j.error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(j.error)},
                  {"creadoEn", double(j.createdAt)}, {"terminadoEn", j.finishedAt ? QJsonValue(double(j.finishedAt)) : QJsonValue(QJsonValue::Null)}};
    if (withLines) o.insert(QStringLiteral("lineas"), QJsonArray::fromStringList(j.lines));
    return o;
}

QJsonObject JobManager::start(const QString &type, const QVariant &libraryId, const QString &title, Fn fn)
{
    std::shared_ptr<Job> job = std::make_shared<Job>();
    {
        QMutexLocker lock(&m_mutex);
        for (const auto &x : std::as_const(m_jobs)) {
            if (x->state != QLatin1String("corriendo")) continue;
            const bool clash = x->libraryId == libraryId || (x->libraryId.isNull() && libraryId.isNull())
                               || (x->type == type && (x->libraryId.isNull() || libraryId.isNull()));
            if (clash) throw Running(x->id);
        }
        job->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job->type = type;
        job->libraryId = libraryId;
        job->title = title;
        job->createdAt = QDateTime::currentMSecsSinceEpoch();
        m_jobs.append(job);
    }
    const QJsonObject first = summary(job->id);
    emit changed(first);

    QPointer<JobManager> self(this);
    (void)QtConcurrent::run([self, job, fn = std::move(fn)] {
        Context ctx;
        ctx.aborted = [job] { return job->abort.load(); };
        ctx.line = [self, job](const QString &t) {
            {
                QMutexLocker lock(&self->m_mutex);
                job->lines.append(t);
                if (job->lines.size() > kMaxLines) job->lines.erase(job->lines.begin(), job->lines.begin() + (job->lines.size() - kMaxLines));
            }
            QMetaObject::invokeMethod(self.data(), [self, id = job->id, t] { if (self) emit self->line(QJsonObject{{"id", id}, {"texto", t}}); }, Qt::QueuedConnection);
        };
        ctx.progress = [self, job](int done, int total) {
            QJsonObject s;
            {
                QMutexLocker lock(&self->m_mutex);
                job->hasProgress = true;
                job->done = done;
                job->total = total;
                s = self->summaryOf(*job, false);
            }
            QMetaObject::invokeMethod(self.data(), [self, s] { if (self) emit self->changed(s); }, Qt::QueuedConnection);
        };
        QJsonValue result;
        QString error;
        try {
            result = fn(ctx);
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        if (!self) return;
        QJsonObject s;
        {
            QMutexLocker lock(&self->m_mutex);
            const bool aborted = job->abort.load();
            job->state = aborted ? QStringLiteral("cancelado") : (error.isEmpty() ? QStringLiteral("completado") : QStringLiteral("error"));
            job->result = error.isEmpty() ? result : QJsonValue(QJsonValue::Null);
            job->error = error;
            job->finishedAt = QDateTime::currentMSecsSinceEpoch();
            s = self->summaryOf(*job, false);
            // Solo se conservan los últimos trabajos terminados.
            QList<std::shared_ptr<Job>> finished;
            for (const auto &x : std::as_const(self->m_jobs)) if (x->state != QLatin1String("corriendo")) finished.append(x);
            std::sort(finished.begin(), finished.end(), [](const auto &a, const auto &b) { return a->finishedAt < b->finishedAt; });
            for (int i = 0; i < int(finished.size()) - kMaxHistory; ++i) self->m_jobs.removeOne(finished.at(i));
        }
        QMetaObject::invokeMethod(self.data(), [self, s] { if (self) emit self->changed(s); }, Qt::QueuedConnection);
    });
    return first;
}

QJsonObject JobManager::summary(const QString &id, bool withLines) const
{
    QMutexLocker lock(&m_mutex);
    for (const auto &j : m_jobs) if (j->id == id) return summaryOf(*j, withLines);
    return {};
}

QJsonArray JobManager::list() const
{
    QMutexLocker lock(&m_mutex);
    QList<std::shared_ptr<Job>> sorted = m_jobs;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a->createdAt > b->createdAt; });
    QJsonArray out;
    for (const auto &j : sorted) out.append(summaryOf(*j, false));
    return out;
}

bool JobManager::exists(const QString &id) const
{
    QMutexLocker lock(&m_mutex);
    for (const auto &j : m_jobs) if (j->id == id) return true;
    return false;
}

bool JobManager::cancel(const QString &id)
{
    QMutexLocker lock(&m_mutex);
    for (const auto &j : m_jobs) if (j->id == id) { if (j->state != QLatin1String("corriendo")) return false; j->abort = true; return true; }
    return false;
}

bool JobManager::busyFor(const QString &libraryId) const
{
    QMutexLocker lock(&m_mutex);
    for (const auto &j : m_jobs) if (j->state == QLatin1String("corriendo") && (j->libraryId.isNull() || j->libraryId.toString() == libraryId)) return true;
    return false;
}

bool JobManager::anyRunning() const
{
    QMutexLocker lock(&m_mutex);
    for (const auto &j : m_jobs) if (j->state == QLatin1String("corriendo")) return true;
    return false;
}

void JobManager::cancelAll()
{
    QMutexLocker lock(&m_mutex);
    for (const auto &j : m_jobs) if (j->state == QLatin1String("corriendo")) j->abort = true;
}

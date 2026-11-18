#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

/** Conversión de filas SQLite a los JSON de la API v1 (specs/heredadas/04 §4). */
namespace Mappers {
QString isoDate(qint64 secs);
QJsonValue nz(const QVariant &v);                    // NULL de SQL → null JSON
QJsonArray genres(const QVariant &v);
QJsonArray people(const QVariant &v);
QJsonObject mediaItem(const QVariantMap &row, bool detail);
QJsonObject serie(const QVariantMap &row, bool detail);
}

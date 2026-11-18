#pragma once
#include <QString>
#include <QVector>

/** Generador de códigos QR propio (modo byte, corrección de errores M, versiones 1-40). Sin dependencias. */
namespace Qr {
/** Matriz de módulos (true = oscuro); vacía si el texto no cabe. */
QVector<QVector<bool>> encode(const QString &text);
/** SVG del código con un margen de `border` módulos (blanco sobre negro). Cadena vacía si no cabe. */
QString svg(const QString &text, int border = 1);
}

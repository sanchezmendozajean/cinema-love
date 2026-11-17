#pragma once
#include <QObject>

class QQuickWindow;

/** Solo pruebas (--ui-test wheel): envía muescas de rueda sintéticas a la ventana para medir el desplazamiento. */
class TestInput : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void wheel(QQuickWindow *window, qreal x, qreal y, int angleDelta);
};

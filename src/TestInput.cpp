#include "TestInput.h"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QWheelEvent>

void TestInput::wheel(QQuickWindow *window, qreal x, qreal y, int angleDelta)
{
    if (!window) return;
    const QPointF pos(x, y);
    QWheelEvent ev(pos, window->mapToGlobal(pos), QPoint(), QPoint(0, angleDelta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &ev);
}

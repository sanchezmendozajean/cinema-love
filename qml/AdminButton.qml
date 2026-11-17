import QtQuick
import CinemaLove

// Botón de la interfaz de administración. primary = acción principal (acento); danger = destructiva (rojo, distinto del acento).
Rectangle {
    id: btn
    property string text
    property bool primary: false
    property bool danger: false
    signal clicked()

    readonly property color base: danger ? Tokens.danger : Tokens.accent
    implicitHeight: 40
    implicitWidth: label.implicitWidth + 32
    width: implicitWidth; height: implicitHeight
    radius: Tokens.radiusSmall + 2
    opacity: enabled ? 1 : 0.4
    color: primary ? (hov.hovered && enabled ? Qt.lighter(base, 1.12) : base)
                   : (hov.hovered && enabled ? Tokens.surfaceHover : Tokens.surface)
    border.width: primary ? 0 : 1
    border.color: danger ? "#66E5484D" : Tokens.borderStrong
    Behavior on color { ColorAnimation { duration: Tokens.durFast } }

    Text { id: label; anchors.centerIn: parent; text: btn.text; font.pixelSize: 14; font.weight: Font.Medium
           color: btn.primary ? "white" : (btn.danger ? Tokens.danger : Tokens.text) }
    HoverHandler { id: hov; cursorShape: btn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler { enabled: btn.enabled; onTapped: btn.clicked() }
    Accessible.role: Accessible.Button; Accessible.name: text
}

import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Casilla con el estilo del tema oscuro (el estilo Basic por defecto dibuja un cuadro blanco).
CheckBox {
    id: c
    spacing: 10
    indicator: Rectangle {
        implicitWidth: 22; implicitHeight: 22
        x: c.leftPadding; y: c.topPadding + (c.availableHeight - height) / 2
        radius: 6
        color: c.checked ? Tokens.accent : Tokens.bg
        border.width: c.visualFocus ? 2 : 1
        border.color: c.checked ? Tokens.accent : (c.hovered ? Tokens.textMuted : Tokens.borderStrong)
        Text { anchors.centerIn: parent; visible: c.checked; text: "✓"; color: "white"; font.pixelSize: 14; font.bold: true }
    }
    contentItem: Text {
        leftPadding: c.indicator.width + c.spacing
        text: c.text; color: Tokens.text; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter
    }
}

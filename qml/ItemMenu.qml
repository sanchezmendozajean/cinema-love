import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Menú contextual ⋯ de una tarjeta. `entries`: [{ label, icon, danger, header, run() }]; cada opción lleva su icono (NavIcon). Se cierra al hacer clic afuera o con Esc.
Popup {
    id: menu
    property var entries: []
    property string heading

    parent: Overlay.overlay
    padding: 6
    width: 280
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: Tokens.radius; color: Tokens.surfaceRaised; border.color: Tokens.borderStrong }

    function showAt(x, y) {
        menu.x = Math.max(8, Math.min(x, (parent ? parent.width : 1280) - menu.width - 8))
        menu.y = Math.max(8, Math.min(y, (parent ? parent.height : 720) - menu.implicitHeight - 8))
        open()
    }

    contentItem: Column {
        spacing: 0
        Text { visible: menu.heading !== ""; width: parent.width; leftPadding: 10; topPadding: 6; bottomPadding: 8; elide: Text.ElideRight
               text: menu.heading; color: Tokens.textFaint; font.pixelSize: 12; font.weight: Font.DemiBold }
        Repeater {
            model: menu.entries
            delegate: Item {
                required property var modelData
                width: parent.width
                height: modelData.header ? 30 : 38
                Text { visible: !!modelData.header; anchors { left: parent.left; leftMargin: 10; bottom: parent.bottom; bottomMargin: 4 }
                       text: modelData.header || ""; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                Rectangle {
                    visible: !modelData.header
                    anchors.fill: parent; radius: Tokens.radiusSmall
                    color: rowHover.hovered ? Tokens.surfaceHover : "transparent"
                    NavIcon {
                        id: entryIcon
                        width: 20; height: 20
                        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                        name: modelData.icon || "info"
                        color: modelData.danger ? Tokens.danger : (rowHover.hovered ? Tokens.text : Tokens.textMuted)
                    }
                    Text { anchors { left: entryIcon.right; leftMargin: 12; right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                           text: modelData.label || ""; elide: Text.ElideRight; font.pixelSize: 14
                           color: modelData.danger ? Tokens.danger : Tokens.text }
                    HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { enabled: !modelData.header; onTapped: { menu.close(); if (modelData.run) modelData.run() } }
                }
            }
        }
    }
}

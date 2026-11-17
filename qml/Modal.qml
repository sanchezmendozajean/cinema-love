import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Diálogo modal (regla del proyecto): siempre con botón X visible y NUNCA se cierra al hacer clic afuera.
Popup {
    id: modal
    property string heading
    default property alias content: body.data
    property int dialogWidth: 520
    property bool dimmed: true          // false: deja ver lo de atrás (p. ej. la vista previa de subtítulos sobre el video)
    property bool atTop: false       // true: arriba y centrado en vez de en el centro

    parent: Overlay.overlay
    anchors.centerIn: atTop ? undefined : parent
    onAboutToShow: if (atTop) { x = Math.round(((parent ? parent.width : 800) - width) / 2); y = 60 }
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape      // sin CloseOnPressOutside
    width: Math.min(dialogWidth, (parent ? parent.width : 800) - 48)
    padding: 0

    Overlay.modal: Rectangle { color: modal.dimmed ? "#B80E0D11" : "transparent" } // --scrim
    background: Rectangle { radius: Tokens.radius + 4; color: Tokens.surfaceRaised; border.color: Tokens.borderStrong }

    contentItem: Column {
        spacing: 0
        Item {
            width: parent.width; height: 60
            Text { anchors { left: parent.left; leftMargin: 24; verticalCenter: parent.verticalCenter; right: closeBtn.left; rightMargin: 12 }
                   text: modal.heading; color: Tokens.text; font.pixelSize: 18; font.weight: Font.DemiBold; elide: Text.ElideRight }
            Rectangle {
                id: closeBtn
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                width: 34; height: 34; radius: 17
                color: xHover.hovered ? Tokens.surfaceHover : "transparent"
                Text { anchors.centerIn: parent; text: "✕"; color: Tokens.textMuted; font.pixelSize: 15 }
                HoverHandler { id: xHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: modal.close() }
                Accessible.role: Accessible.Button; Accessible.name: "Cerrar"
            }
        }
        Rectangle { width: parent.width; height: 1; color: Tokens.border }
        Item {
            width: parent.width
            height: body.childrenRect.height + 48
            Item { id: body; x: 24; y: 24; width: parent.width - 48; height: childrenRect.height }
        }
    }
}

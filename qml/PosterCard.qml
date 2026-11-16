import QtQuick
import CinemaLove

// Tarjeta de póster (design/12): delegado barato — un Image y dos Text.
Item {
    id: card
    property string title
    property int year
    property string posterUrl
    property bool available: true
    property string progressState
    property real progressFraction: 0
    signal activated()

    readonly property bool hovered: hover.hovered

    Rectangle {
        id: frame
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
        height: width * 1.5
        radius: Tokens.radius
        color: Tokens.surface
        border.width: card.activeFocus || card.hovered ? 2 : 1
        border.color: card.activeFocus || card.hovered ? Tokens.accent : Tokens.border
        scale: card.hovered ? 1.03 : 1.0
        Behavior on scale { NumberAnimation { duration: Tokens.durFast; easing.type: Easing.OutCubic } }

        Image {
            id: img
            anchors { fill: parent; margins: 1 }
            source: card.posterUrl
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 400
            opacity: status === Image.Ready ? (card.available ? 1 : 0.35) : 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
        // Marcador mientras carga o si no hay póster
        Text {
            anchors { fill: parent; margins: 12 }
            visible: img.status !== Image.Ready
            text: card.title
            color: Tokens.textFaint
            font.pixelSize: 14; font.bold: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Rectangle { // progreso (en curso)
            visible: card.progressState === "enProgreso" && card.progressFraction > 0
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 4; color: "#99000000"
            Rectangle { height: parent.height; width: parent.width * card.progressFraction; color: Tokens.accent }
        }
        Rectangle { // visto
            visible: card.progressState === "visto"
            anchors { right: parent.right; top: parent.top; margins: 8 }
            width: 24; height: 24; radius: 12; color: "#E64CAF7D"
            Text { anchors.centerIn: parent; text: "✓"; color: "white"; font.pixelSize: 14; font.bold: true }
        }
        Rectangle { // «No disponible» si el archivo no está en el disco
            visible: !card.available
            anchors { left: parent.left; bottom: parent.bottom; margins: 8 }
            width: unavailableText.width + 14; height: 22; radius: 4; color: "#CC000000"
            Text { id: unavailableText; anchors.centerIn: parent; text: "Sin acceso al archivo"; color: Tokens.textMuted; font.pixelSize: 11 }
        }
    }
    Text {
        anchors { left: frame.left; right: frame.right; top: frame.bottom; topMargin: 8 }
        text: card.title
        color: Tokens.text; font.pixelSize: 13; font.weight: Font.Medium
        elide: Text.ElideRight
    }
    Text {
        anchors { left: frame.left; right: frame.right; top: frame.bottom; topMargin: 27 }
        text: card.year > 0 ? card.year : ""
        color: Tokens.textMuted; font.pixelSize: 12
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: { card.forceActiveFocus(); card.activated() } }
    Keys.onReturnPressed: card.activated()
    Keys.onEnterPressed: card.activated()
    Keys.onSpacePressed: card.activated()
}

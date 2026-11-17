import QtQuick
import CinemaLove

// Tarjeta apaisada 16:9 (design/12) para «Continuar viendo»: backdrop, barra de progreso y título.
Item {
    id: card
    property string title
    property int year
    property string subtitle
    property string imageUrl
    property real progressFraction: 0
    property string remaining
    signal activated()
    signal menuRequested(real x, real y)
    readonly property bool hovered: hover.hovered

    Rectangle {
        id: frame
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
        height: width * 9 / 16
        radius: Tokens.radius; color: Tokens.surface
        border.width: card.activeFocus || card.hovered ? 2 : 1
        border.color: card.activeFocus || card.hovered ? Tokens.accent : Tokens.border
        scale: card.hovered ? 1.03 : 1.0
        Behavior on scale { NumberAnimation { duration: Tokens.durFast; easing.type: Easing.OutCubic } }
        Image {
            id: img
            anchors { fill: parent; margins: 1 }
            source: Images.src(card.imageUrl); asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 560
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
        Text { anchors { fill: parent; margins: 12 } visible: img.status !== Image.Ready; text: card.title; color: Tokens.textFaint
               font.pixelSize: 14; font.bold: true; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        Rectangle {
            id: menuBtn
            visible: card.hovered || card.activeFocus || menuHover.hovered
            anchors { left: parent.left; top: parent.top; margins: 8 }
            width: 30; height: 30; radius: 15
            color: menuHover.hovered ? "#E6000000" : "#99000000"
            Text { anchors.centerIn: parent; text: "⋯"; color: "white"; font.pixelSize: 20; font.bold: true; bottomPadding: 4 }
            HoverHandler { id: menuHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: { const p = menuBtn.mapToItem(null, 0, menuBtn.height + 4); card.menuRequested(p.x, p.y) } }
        }
        Rectangle { // oscurece la base para leer el tiempo restante
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 44
            gradient: Gradient { GradientStop { position: 0; color: "#00000000" } GradientStop { position: 1; color: "#CC000000" } }
        }
        Text { anchors { left: parent.left; bottom: parent.bottom; leftMargin: 12; bottomMargin: 12 }
               text: card.remaining; color: "#E8E6EB"; font.pixelSize: 12; font.weight: Font.Medium }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 4; color: "#99000000"
            Rectangle { height: parent.height; width: parent.width * card.progressFraction; color: Tokens.accent }
        }
    }
    Text { anchors { left: frame.left; right: frame.right; top: frame.bottom; topMargin: 8 }
           text: card.title; color: Tokens.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
    Text { anchors { left: frame.left; right: frame.right; top: frame.bottom; topMargin: 27 }
           text: card.subtitle !== "" ? card.subtitle : (card.year > 0 ? card.year : ""); color: Tokens.textMuted; font.pixelSize: 12 }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: { if (menuHover.hovered) return; card.forceActiveFocus(); card.activated() } }
    Keys.onReturnPressed: card.activated()
    Keys.onSpacePressed: card.activated()
}

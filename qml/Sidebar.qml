import QtQuick
import CinemaLove

// Barra lateral de secciones (design/11 · Shell). Ancha con etiquetas; compacta (solo iconos) en ventanas estrechas.
Rectangle {
    id: bar
    property string section: "home"
    property bool compact: false
    signal navigate(string section)
    signal aboutRequested()

    color: Tokens.surface
    Rectangle { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: 1; color: Tokens.border }

    readonly property var mainItems: [
        { k: "home", t: "Inicio", icon: "home" },
        { k: "catalog", t: "Películas", icon: "film" },
        { k: "series", t: "Series", icon: "tv" },
        { k: "collections", t: "Colecciones", icon: "collections" }
    ]

    // Marca (ocupa la franja de la barra de título)
    Text {
        visible: !bar.compact
        anchors { left: parent.left; leftMargin: 22; top: parent.top; topMargin: 9 }
        text: "CinemaLove"; color: Tokens.text; font.pixelSize: 17; font.family: "Georgia"; font.weight: Font.DemiBold
    }

    Column {
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 56; leftMargin: 10; rightMargin: 10 }
        spacing: 4
        Repeater {
            model: bar.mainItems
            delegate: NavItem { width: parent.width; entry: modelData; selected: bar.section === modelData.k; compact: bar.compact; onClicked: bar.navigate(modelData.k) }
        }
    }

    Column {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 14; leftMargin: 10; rightMargin: 10 }
        spacing: 4
        Rectangle { width: parent.width; height: 1; color: Tokens.border; anchors.horizontalCenter: parent.horizontalCenter }
        Item { width: 1; height: 6 }
        NavItem { width: parent.width; entry: ({ k: "admin", t: "Administración", icon: "admin" }); selected: bar.section === "admin"; compact: bar.compact; onClicked: bar.navigate("admin") }
        NavItem { width: parent.width; entry: ({ k: "about", t: "Acerca de", icon: "info" }); selected: false; compact: bar.compact; onClicked: bar.aboutRequested() }
    }

    component NavItem: Rectangle {
        id: item
        property var entry
        property bool selected: false
        property bool compact: false
        signal clicked()
        height: 44; radius: Tokens.radiusSmall + 2
        color: selected ? "#24E23F52" : (hover.hovered ? Tokens.surfaceHover : "transparent")
        Behavior on color { ColorAnimation { duration: Tokens.durFast } }
        Rectangle { // indicador de la sección activa
            visible: item.selected
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: 3; height: 22; radius: 2; color: Tokens.accent
        }
        NavIcon {
            id: ic
            name: item.entry.icon
            color: item.selected ? Tokens.accentSoft : (hover.hovered ? Tokens.text : Tokens.textMuted)
            x: item.compact ? (item.width - width) / 2 : 16
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: !item.compact
            anchors { left: ic.right; leftMargin: 14; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            text: item.entry.t; elide: Text.ElideRight; font.pixelSize: 15; font.weight: item.selected ? Font.DemiBold : Font.Medium
            color: item.selected ? Tokens.text : (hover.hovered ? Tokens.text : Tokens.textMuted)
        }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: item.clicked() }
        Accessible.role: Accessible.Button; Accessible.name: item.entry.t
    }
}

import QtQuick
import CinemaLove

// Fila con título y scroll horizontal (RF-11). Se oculta sola si está vacía.
Item {
    id: row
    property string title
    property var items: []
    property bool landscape: false
    signal chosen(string movieId)

    visible: items.length > 0
    height: visible ? 52 + list.height : 0
    readonly property real cardW: landscape ? 300 : 170

    Text {
        x: 40; y: 0
        text: row.title; color: Tokens.text; font.pixelSize: 22; font.weight: Font.DemiBold
    }
    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 44 }
        height: landscape ? row.cardW * 9 / 16 + 60 : row.cardW * 1.5 + 60
        orientation: ListView.Horizontal
        leftMargin: 34; rightMargin: 34
        clip: true; reuseItems: true; cacheBuffer: 800
        boundsBehavior: Flickable.StopAtBounds
        model: row.items
        delegate: Loader {
            required property var modelData
            width: row.cardW; height: list.height
            sourceComponent: row.landscape ? landscapeC : posterC
            onLoaded: item.d = modelData
        }
    }
    Component { id: posterC; PosterCard {
        property var d: ({})
        title: d.title || ""; year: d.year || 0; posterUrl: d.posterUrl || ""
        progressState: d.progressState || ""; progressFraction: d.progressFraction || 0
        onActivated: row.chosen(d.id) } }
    Component { id: landscapeC; LandscapeCard {
        property var d: ({})
        title: d.title || ""; year: d.year || 0; imageUrl: d.backdropUrl || d.posterUrl || ""
        progressFraction: d.progressFraction || 0
        remaining: d.duration > 0 ? "Quedan " + Math.max(1, Math.round((d.duration - d.progressPos) / 60)) + " min" : ""
        onActivated: row.chosen(d.id) } }
}

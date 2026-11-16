import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Inicio (design/11 · P2, RF-11): héroe con lo último que se estaba viendo y filas por categoría.
Rectangle {
    id: view
    color: Tokens.bg
    property var model            // MovieModel
    signal openDetail(string movieId)
    signal playMovie(string movieId)

    property var cont: []
    property var queue: []
    property var recent: []
    property var watched: []
    readonly property var featured: cont.length > 0 ? cont[0] : (recent.length > 0 ? recent[0] : null)
    readonly property bool heroContinue: cont.length > 0

    function refresh() {
        if (!model) return
        cont = model.homeRow("continue", 20)
        queue = model.homeRow("queue", 20)
        recent = model.homeRow("recent", 20)
        watched = model.homeRow("watched", 20)
    }
    onModelChanged: refresh()
    Component.onCompleted: refresh()
    Connections { target: view.model; function onHomeChanged() { view.refresh() } function onCountChanged() { view.refresh() } }

    function dur(sec) { const m = Math.round(sec / 60), h = Math.floor(m / 60); return h > 0 ? h + " h " + (m % 60) + " min" : m + " min" }

    function scrollTo(y) { flick.contentY = y }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: rows.y + rows.height + 56
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { contentItem: Rectangle { implicitWidth: 6; radius: 3; color: Tokens.borderStrong } }

        Item {
            id: hero
            width: parent.width
            height: Math.max(400, view.height * 0.62)
            Image {
                anchors.fill: parent
                source: view.featured ? view.featured.backdropUrl : ""
                asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 1280
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 400 } }
            }
            Rectangle { anchors.fill: parent; gradient: Gradient {
                GradientStop { position: 0.0; color: "#660E0D11" }
                GradientStop { position: 0.45; color: "#000E0D11" }
                GradientStop { position: 1.0; color: Tokens.bg } } }
            Rectangle { anchors.fill: parent; gradient: Gradient { orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#F20E0D11" }
                GradientStop { position: 0.4; color: "#CC0E0D11" }
                GradientStop { position: 0.8; color: "#260E0D11" }
                GradientStop { position: 1.0; color: "#000E0D11" } } }

            Column {
                visible: view.featured !== null
                x: 56; anchors.bottom: parent.bottom; anchors.bottomMargin: 72
                width: Math.min(parent.width - 112, 640)
                spacing: 10
                Text { text: view.heroContinue ? "CONTINUAR VIENDO" : "AGREGADA RECIENTEMENTE"; color: Tokens.accentSoft
                       font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                Text { width: parent.width; text: view.featured ? view.featured.title : ""; color: Tokens.text; font.pixelSize: 52
                       font.family: "Georgia"; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                Text { width: parent.width; elide: Text.ElideRight; color: Tokens.textMuted; font.pixelSize: 15
                       text: { if (!view.featured) return ""; const p = []; if (view.featured.year > 0) p.push(view.featured.year)
                               if (view.featured.duration > 0) p.push(view.dur(view.featured.duration)); if (view.featured.genres) p.push(view.featured.genres.replace(/ · /g, ", "))
                               return p.join("  ·  ") } }
                Text { width: parent.width; text: view.featured ? view.featured.synopsis : ""; color: "#D6D3DB"; font.pixelSize: 15; lineHeight: 1.4
                       wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                Item { width: 1; height: 4 }
                Row {
                    spacing: 12
                    Rectangle {
                        height: 48; width: heroPlay.width + 52; radius: Tokens.radius
                        color: ph.hovered ? Tokens.accentSoft : Tokens.accent
                        Behavior on color { ColorAnimation { duration: Tokens.durFast } }
                        Text { id: heroPlay; anchors.centerIn: parent; color: "white"; font.pixelSize: 15; font.weight: Font.DemiBold
                               text: view.heroContinue ? "▶  Continuar · quedan " + view.dur(view.featured.duration - view.featured.progressPos) : "▶  Reproducir" }
                        HoverHandler { id: ph; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: view.playMovie(view.featured.id) }
                    }
                    Rectangle {
                        height: 48; width: infoLabel.width + 44; radius: Tokens.radius
                        color: ih.hovered ? "#40FFFFFF" : "#2AFFFFFF"; border.color: "#33FFFFFF"
                        Text { id: infoLabel; anchors.centerIn: parent; text: "ⓘ  Más información"; color: Tokens.text; font.pixelSize: 15; font.weight: Font.Medium }
                        HoverHandler { id: ih; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: view.openDetail(view.featured.id) }
                    }
                }
                Rectangle { visible: view.heroContinue; width: 360; height: 4; radius: 2; color: "#40FFFFFF"
                    Rectangle { height: parent.height; radius: 2; color: Tokens.accent; width: view.featured ? parent.width * view.featured.progressFraction : 0 } }
            }
        }

        Column {
            id: rows
            y: hero.height - 40
            width: parent.width
            spacing: 24
            HomeRow { width: parent.width; title: "Continuar viendo"; items: view.cont; landscape: true; onChosen: (id) => view.openDetail(id) }
            HomeRow { width: parent.width; title: "Próximas a ver"; items: view.queue; onChosen: (id) => view.openDetail(id) }
            HomeRow { width: parent.width; title: "Agregados recientemente"; items: view.recent; onChosen: (id) => view.openDetail(id) }
            HomeRow { width: parent.width; title: "Vistos recientemente"; items: view.watched; onChosen: (id) => view.openDetail(id) }
        }
    }

    Column { // estado vacío / error
        anchors.centerIn: parent; spacing: 8
        visible: view.model && view.model.error !== ""
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "No se pudo abrir el catálogo"; color: Tokens.text; font.pixelSize: 20; font.weight: Font.DemiBold }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: view.model ? view.model.error : ""; color: Tokens.textMuted; font.pixelSize: 14 }
    }
}

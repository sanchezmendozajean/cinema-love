import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Catálogo de películas: cabecera con búsqueda + grilla de pósters. Spike de la Fase 2.
Rectangle {
    id: view
    color: Tokens.bg
    property alias model: grid.model
    property int total: 0
    property int matches: 0                 // elementos que cumplen la búsqueda (todas las páginas)
    property int page: 0
    property int pageCount: 1
    signal pageRequested(int page)
    onPageChanged: grid.positionViewAtBeginning()
    property string error: ""
    property string eyebrow: "BIBLIOTECA"
    property string heading: "Películas"
    property string placeholder: "Buscar película…"
    readonly property real scrollY: grid.contentY
    signal search(string text)
    function setSearch(t) { searchField.text = t }
    signal movieChosen(int row)
    property string itemKind: "movie"            // movie | series (para el menú ⋯)
    signal menuRequested(var item, real x, real y)

    Column {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 48; leftMargin: 40; rightMargin: 40 }
        spacing: 4
        Text { text: view.eyebrow; color: Tokens.accentSoft; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
        Item {
            width: parent.width; height: 56
            Text {
                id: h1
                anchors.verticalCenter: parent.verticalCenter
                text: view.heading; color: Tokens.text
                font.pixelSize: 40; font.family: "Georgia"; font.weight: Font.DemiBold
            }
            Text {
                anchors { left: h1.right; leftMargin: 14; baseline: h1.baseline }
                text: view.matches === view.total ? view.total + " títulos" : view.matches + " de " + view.total
                color: Tokens.textMuted; font.pixelSize: 14
            }
            TextField {
                id: searchField
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                width: 300; height: 40
                placeholderText: view.placeholder
                placeholderTextColor: Tokens.textFaint
                color: Tokens.text; font.pixelSize: 14
                leftPadding: 14; rightPadding: 34
                selectByMouse: true
                background: Rectangle {
                    radius: Tokens.radius; color: Tokens.surface
                    border.width: searchField.activeFocus ? 2 : 1
                    border.color: searchField.activeFocus ? Tokens.accent : Tokens.border
                }
                onTextChanged: view.search(text)
                Keys.onEscapePressed: { text = ""; grid.forceActiveFocus() }
                Keys.onDownPressed: grid.forceActiveFocus()
                Text { // botón de limpiar
                    visible: searchField.text.length > 0
                    anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    text: "✕"; color: Tokens.textMuted; font.pixelSize: 13
                    TapHandler { onTapped: searchField.text = "" }
                }
            }
        }
    }

    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: pager.visible ? pager.top : parent.bottom; topMargin: 24; leftMargin: 34; rightMargin: 34 }
        clip: true
        focus: true
        reuseItems: true
        cacheBuffer: 600
        boundsBehavior: Flickable.StopAtBounds
        readonly property int columns: Math.max(2, Math.floor(width / 190))
        cellWidth: Math.floor(width / columns)
        cellHeight: cellWidth * 1.5 + 54
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            contentItem: Rectangle { implicitWidth: 6; radius: 3; color: Tokens.borderStrong }
        }
        delegate: PosterCard {
            required property var model
            required property int index
            width: grid.cellWidth; height: grid.cellHeight
            title: model.title
            year: model.year
            subtitle: model.subtitle
            posterUrl: model.posterUrl
            progressState: model.progressState
            progressFraction: model.progressFraction
            onActivated: view.movieChosen(index)
            onMenuRequested: (x, y) => view.menuRequested({ id: model.movieId, kind: view.itemKind, title: model.title }, x, y)
        }
        KeyNavigation.up: searchField
    }
    SmoothWheel { flickable: grid }

    // Estados vacío / error (design: estados presentes)
    Column {
        anchors.centerIn: grid
        spacing: 8
        visible: view.error !== "" || grid.count === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: view.error !== "" ? "No se pudo abrir el catálogo" : "Sin resultados"
            color: Tokens.text; font.pixelSize: 20; font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(520, view.width - 80); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: view.error !== "" ? view.error : "Prueba con otro título."
            color: Tokens.textMuted; font.pixelSize: 14
        }
    }

    Pager {
        id: pager
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        page: view.page; pageCount: view.pageCount; total: view.matches
        onGo: (p) => view.pageRequested(p)
    }
}

import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Colecciones y Próximas a ver (design/11 · P8/P9). Lista de colecciones con mosaico 2×2 y detalle con grilla de títulos.
Rectangle {
    id: view
    color: Tokens.bg
    property var api                         // ApiClient
    property bool backendReady: false
    signal openItem(var item)                // item: elemento del catálogo (kind, id, seriesId…)
    signal menuRequested(var item, real x, real y)
    signal toast(string text)
    signal changed()                         // se modificó una colección

    property var collections: []
    property int watchlistCount: 0
    property var current: null               // null = lista; { special: true } = Próximas a ver; { id, nombre, quitarAlVerse }
    property var items: []
    property string error: ""

    function call(method, path, body, cb) { api.request(method, "/api/v1" + path, body === undefined ? null : body, cb) }

    function loadList() {
        call("GET", "/collections", undefined, (st, d, err) => {
            if (st === 200) { collections = d; error = "" } else error = err || "No se pudieron cargar las colecciones."
        })
        call("GET", "/watchlist", undefined, (st, d) => { if (st === 200) watchlistCount = d.length })
    }
    function loadItems() {
        if (!current) return
        const path = current.special ? "/watchlist?formato=catalogo" : "/collections/" + current.id + "/items?formato=catalogo"
        call("GET", path, undefined, (st, d, err) => { if (st === 200) items = d; else toast(err || "No se pudo cargar la colección.") })
    }
    function reload() { loadList(); loadItems() }
    function open(c) { current = c; items = []; loadItems() }
    function back() { current = null; items = []; loadList() }

    onBackendReadyChanged: if (backendReady) loadList()
    onVisibleChanged: if (visible && backendReady) reload()
    Keys.onEscapePressed: if (current) back()

    // Cabecera
    Column {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 48; leftMargin: 40; rightMargin: 40 }
        spacing: 4
        Text { text: view.current ? "‹  COLECCIONES" : "TUS LISTAS"; color: Tokens.accentSoft; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5
               TapHandler { enabled: !!view.current; onTapped: view.back() }
               HoverHandler { cursorShape: view.current ? Qt.PointingHandCursor : Qt.ArrowCursor } }
        Item {
            width: parent.width; height: 56
            Text { id: h1; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 40; font.family: "Georgia"; font.weight: Font.DemiBold; color: Tokens.text
                   text: view.current ? (view.current.special ? "Próximas a ver" : view.current.nombre) : "Colecciones" }
            Text { anchors { left: h1.right; leftMargin: 14; baseline: h1.baseline } color: Tokens.textMuted; font.pixelSize: 14
                   text: view.current ? view.items.length + (view.items.length === 1 ? " título" : " títulos") : view.collections.length + (view.collections.length === 1 ? " colección" : " colecciones") }
            Row {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 10
                AdminButton { visible: !view.current; text: "＋  Nueva colección"; primary: true; onClicked: newCol.open() }
                AdminButton { visible: !!view.current && !view.current.special; text: "Renombrar…"; onClicked: renameCol.open() }
                AdminButton { visible: !!view.current && !view.current.special; text: "Eliminar…"; danger: true; onClicked: delCol.open() }
            }
        }
    }

    // --- Lista ---
    Flickable {
        id: listFlick
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 28; leftMargin: 40; rightMargin: 40 }
        visible: !view.current
        contentHeight: flow.height + 40
        clip: true; boundsBehavior: Flickable.StopAtBounds
        Flow {
            id: flow
            width: parent.width; spacing: 20
            // Tarjeta especial fija: Próximas a ver
            Item {
                width: 250; height: 232
                Rectangle {
                    id: wFrame
                    width: 250; height: 188; radius: Tokens.radius; color: Tokens.surface
                    border.width: wHover.hovered ? 2 : 1; border.color: wHover.hovered ? Tokens.accent : Tokens.border
                    Column { anchors.centerIn: parent; spacing: 8
                        Canvas { // marcador (bookmark) en el color de acento
                            anchors.horizontalCenter: parent.horizontalCenter; width: 34; height: 46
                            onPaint: { const c = getContext("2d"); c.reset(); c.fillStyle = Tokens.accent
                                       c.beginPath(); c.moveTo(3, 2); c.lineTo(31, 2); c.lineTo(31, 44); c.lineTo(17, 33); c.lineTo(3, 44); c.closePath(); c.fill() } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: view.watchlistCount + (view.watchlistCount === 1 ? " título" : " títulos"); color: Tokens.textMuted; font.pixelSize: 13 } }
                }
                Text { y: 196; width: 250; text: "Próximas a ver"; color: Tokens.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight }
                HoverHandler { id: wHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: view.open({ special: true }) }
            }
            Repeater {
                model: view.collections
                delegate: Item {
                    id: colCard
                    required property var modelData
                    readonly property var col: modelData      // el Repeater interno del mosaico tapa a `modelData`
                    width: 250; height: 232
                    Rectangle {
                        width: 250; height: 188; radius: Tokens.radius; color: Tokens.surface; clip: true
                        Grid { // mosaico 2×2 de pósters
                            anchors.fill: parent
                            columns: 2
                            Repeater {
                                model: 4
                                delegate: Rectangle {
                                    width: (parent.width) / 2; height: parent.height / 2; color: Tokens.surfaceRaised
                                    Image { anchors.fill: parent; asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 260
                                            source: index < colCard.col.posters.length ? Images.src(colCard.col.posters[index]) : "" }
                                }
                            }
                        }
                        RoundedCorners { anchors.fill: parent; radius: Tokens.radius }
                        Rectangle { anchors.fill: parent; radius: Tokens.radius; color: "transparent"
                                    border.width: cHover.hovered ? 2 : 1; border.color: cHover.hovered ? Tokens.accent : Tokens.border }
                    }
                    Text { y: 196; width: 250; text: modelData.nombre; color: Tokens.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight }
                    Text { y: 216; width: 250; text: modelData.conteo + (modelData.conteo === 1 ? " título" : " títulos") + (modelData.quitarAlVerse ? "  ·  se quitan al verlas" : "")
                           color: Tokens.textMuted; font.pixelSize: 12; elide: Text.ElideRight }
                    HoverHandler { id: cHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: view.open(modelData) }
                }
            }
        }
    }
    SmoothWheel { flickable: listFlick }
    Text { anchors.centerIn: parent; visible: !view.current && view.error !== ""; text: view.error; color: Tokens.textMuted; font.pixelSize: 14 }

    // --- Detalle ---
    GridView {
        id: grid
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 24; leftMargin: 34; rightMargin: 34 }
        visible: !!view.current
        clip: true; reuseItems: true; cacheBuffer: 600
        boundsBehavior: Flickable.StopAtBounds
        readonly property int columns: Math.max(2, Math.floor(width / 190))
        cellWidth: Math.floor(width / columns)
        cellHeight: cellWidth * 1.5 + 54
        model: view.items
        delegate: PosterCard {
            required property var modelData
            width: grid.cellWidth; height: grid.cellHeight
            title: modelData.titulo || ""
            year: modelData.anio || 0
            subtitle: modelData.kind === "episodio" ? (modelData.serieTitulo || "Episodio") : ""
            posterUrl: modelData.posterUrl || ""
            progressState: modelData.progreso ? modelData.progreso.estado : ""
            progressFraction: modelData.progreso && modelData.duracionSegundos > 0 ? Math.min(1, modelData.progreso.posicionSegundos / modelData.duracionSegundos) : 0
            onActivated: view.openItem(modelData)
            onMenuRequested: (x, y) => view.menuRequested({ id: modelData.id, kind: modelData.kind === "pelicula" ? "movie" : modelData.kind, title: modelData.titulo,
                                                            collectionId: view.current && !view.current.special ? view.current.id : "" }, x, y)
        }
    }
    SmoothWheel { flickable: grid }
    Column {
        anchors.centerIn: grid; spacing: 8
        visible: !!view.current && view.items.length === 0
        Text { anchors.horizontalCenter: parent.horizontalCenter; color: Tokens.text; font.pixelSize: 20; font.weight: Font.DemiBold; text: "Aquí aún no hay nada" }
        Text { anchors.horizontalCenter: parent.horizontalCenter; color: Tokens.textMuted; font.pixelSize: 14
               text: view.current && view.current.special ? "Usa el menú ⋯ de cualquier título › Agregar a Próximas a ver." : "Agrega películas desde su menú ⋯ › Agregar a colección." }
    }

    // --- Diálogos ---
    Modal {
        id: newCol
        heading: "Nueva colección"
        onOpened: { newName.text = ""; newAuto.checked = false; newErr.text = ""; newName.forceActiveFocus() }
        Column {
            width: parent.width; spacing: 14
            Text { text: "Nombre"; color: Tokens.textMuted; font.pixelSize: 12 }
            TextField { id: newName; width: parent.width; height: 42; color: Tokens.text; font.pixelSize: 14; leftPadding: 12; maximumLength: 200
                background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: newName.activeFocus ? Tokens.accent : Tokens.border }
                Keys.onReturnPressed: createBtn.clicked() }
            Check { id: newAuto; text: "Quitar de la colección al marcar como vista" }
            Text { id: newErr; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; visible: text !== "" }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: newCol.close() }
                AdminButton { id: createBtn; text: "Crear"; primary: true; enabled: newName.text.trim() !== ""
                    onClicked: view.call("POST", "/collections", { nombre: newName.text.trim(), quitarAlVerse: newAuto.checked }, (st, d, err) => {
                        if (st === 200 || st === 201) { newCol.close(); view.loadList(); view.toast("Colección creada."); view.changed() } else newErr.text = err || "No se pudo crear." }) }
            }
        }
    }
    Modal {
        id: renameCol
        heading: "Renombrar colección"
        onOpened: { renName.text = view.current ? view.current.nombre : ""; renAuto.checked = view.current ? view.current.quitarAlVerse : false; renErr.text = ""; renName.forceActiveFocus() }
        Column {
            width: parent.width; spacing: 14
            TextField { id: renName; width: parent.width; height: 42; color: Tokens.text; font.pixelSize: 14; leftPadding: 12; maximumLength: 200
                background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: renName.activeFocus ? Tokens.accent : Tokens.border } }
            Check { id: renAuto; text: "Quitar de la colección al marcar como vista" }
            Text { id: renErr; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; visible: text !== "" }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: renameCol.close() }
                AdminButton { text: "Guardar"; primary: true; enabled: renName.text.trim() !== ""
                    onClicked: view.call("PUT", "/collections/" + view.current.id, { nombre: renName.text.trim(), quitarAlVerse: renAuto.checked }, (st, d, err) => {
                        if (st === 200) { view.current = { id: d.id, nombre: d.nombre, quitarAlVerse: d.quitarAlVerse }; renameCol.close(); view.changed() } else renErr.text = err || "No se pudo guardar." }) }
            }
        }
    }
    Modal {
        id: delCol
        heading: "Eliminar colección"
        Column {
            width: parent.width; spacing: 16
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14; lineHeight: 1.35
                   text: view.current && !view.current.special ? "Se eliminará la colección «" + view.current.nombre + "» y su lista de títulos. Las películas y series NO se borran." : "" }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: delCol.close() }
                AdminButton { text: "Eliminar colección"; danger: true; primary: true
                    onClicked: view.call("DELETE", "/collections/" + view.current.id, null, (st, d, err) => {
                        delCol.close()
                        if (st === 200) { view.toast("Colección eliminada."); view.back(); view.changed() } else view.toast(err || "No se pudo eliminar.") }) }
            }
        }
    }
}

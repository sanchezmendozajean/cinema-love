import QtQuick
import QtQuick.Effects
import CinemaLove

// Ficha de persona (RF-55): foto, nombre, datos y biografía (TMDb vía el backend) y «En tu colección» (títulos del catálogo donde participa).
Rectangle {
    id: view
    color: Tokens.bg
    property var api
    property var person: null          // { id, nombre, fotoUrl } mínimo al abrir
    property var info: ({})
    property var items: []
    property bool loaded: false
    property bool bioExpanded: false
    signal back()
    signal openItem(var item)
    signal menuRequested(var item, real x, real y)

    onPersonChanged: {
        info = person ? { nombre: person.nombre, fotoUrl: person.fotoUrl } : ({})
        items = []; loaded = false; bioExpanded = false
        if (!person || !person.id || !api) { loaded = true; return }
        const id = person.id
        api.request("GET", "/api/v1/people/" + id, null, (st, d) => { if (view.person && view.person.id === id && st === 200) view.info = d })
        api.request("GET", "/api/v1/people/" + id + "/in-catalog", null, (st, d) => {
            if (view.person && view.person.id === id) { if (st === 200) view.items = d; view.loaded = true } })
    }
    Keys.onEscapePressed: view.back()

    function fecha(iso) {
        if (!iso) return ""
        const m = /^(\d{4})-(\d{2})-(\d{2})/.exec(iso)
        if (!m) return iso
        const meses = ["enero","febrero","marzo","abril","mayo","junio","julio","agosto","septiembre","octubre","noviembre","diciembre"]
        return Number(m[3]) + " de " + meses[Number(m[2]) - 1] + " de " + m[1]
    }
    function edad(n, f) {
        const a = new Date(n), b = f ? new Date(f) : new Date()
        let e = b.getFullYear() - a.getFullYear()
        if (b.getMonth() < a.getMonth() || (b.getMonth() === a.getMonth() && b.getDate() < a.getDate())) e--
        return e
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, content.y + content.height + 64)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Item {
            id: content
            x: 56; y: 72
            width: parent.width - 112
            height: Math.max(head.height, 0) + (grid.visible ? grid.height + 40 : 0) + (empty.visible ? 60 : 0)

            Item {
                id: head
                width: parent.width
                height: Math.max(photoBox.height, textCol.height)
                Rectangle {
                    id: photoBox
                    width: 220; height: 220; radius: 110; color: Tokens.surface; border.color: Tokens.border
                    Text { anchors.centerIn: parent; visible: photo.status !== Image.Ready; color: Tokens.textFaint; font.pixelSize: 80
                           text: (view.info.nombre || "?").charAt(0) }
                    Image {
                        id: photo
                        anchors { fill: parent; margins: 1 }
                        source: Images.src(view.info.fotoUrl)
                        asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 440
                        visible: status === Image.Ready
                        layer.enabled: true
                        layer.effect: MultiEffect { maskEnabled: true; maskSource: photoMask; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0 }
                    }
                    Rectangle { id: photoMask; anchors.fill: parent; radius: width / 2; layer.enabled: true; visible: false }
                }
                Column {
                    id: textCol
                    x: photoBox.width + 40
                    width: Math.min(parent.width - x, 820)
                    spacing: 10
                    Text { width: parent.width; text: view.info.nombre || ""; color: Tokens.text; font.pixelSize: 44; font.family: Tokens.fontDisplay; font.weight: Font.DemiBold
                           wrapMode: Text.WordWrap }
                    Text { visible: text !== ""; color: Tokens.accentSoft; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 1.2
                           text: (view.info.departamento || "").toUpperCase() }
                    Text { visible: text !== ""; width: parent.width; color: Tokens.textMuted; font.pixelSize: 14; wrapMode: Text.WordWrap
                           text: {
                               const p = []
                               if (view.info.nacimiento) p.push("Nació el " + view.fecha(view.info.nacimiento) + (view.info.fallecimiento ? "" : " (" + view.edad(view.info.nacimiento) + " años)"))
                               if (view.info.fallecimiento) p.push("Falleció el " + view.fecha(view.info.fallecimiento) + (view.info.nacimiento ? " (a los " + view.edad(view.info.nacimiento, view.info.fallecimiento) + " años)" : ""))
                               if (view.info.lugarNacimiento) p.push(view.info.lugarNacimiento)
                               return p.join("  ·  ")
                           } }
                    Item { width: 1; height: 6; visible: bio.visible }
                    Text {
                        id: bio
                        visible: !!view.info.biografia
                        width: parent.width; color: Tokens.text; font.pixelSize: 15; lineHeight: 1.45; wrapMode: Text.WordWrap
                        maximumLineCount: view.bioExpanded ? 1000 : 7; elide: Text.ElideRight
                        text: view.info.biografia || ""
                    }
                    Text { visible: bio.visible && (bio.truncated || view.bioExpanded); color: Tokens.accentSoft; font.pixelSize: 13; font.weight: Font.Medium
                           text: view.bioExpanded ? "Mostrar menos" : "Leer más"
                           HoverHandler { cursorShape: Qt.PointingHandCursor }
                           TapHandler { onTapped: view.bioExpanded = !view.bioExpanded } }
                    Text { visible: view.loaded && !view.info.biografia && !!view.person && !!view.person.id; color: Tokens.textFaint; font.pixelSize: 13
                           text: "Sin biografía disponible (requiere la clave de TMDb y conexión)." }
                }
            }

            Column {
                id: grid
                y: head.height + 40
                width: parent.width; spacing: 14
                visible: view.items.length > 0
                Text { text: "EN TU COLECCIÓN  ·  " + view.items.length; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                Flow {
                    id: flow
                    width: parent.width
                    readonly property int columns: Math.max(2, Math.floor(width / 190))
                    readonly property real cell: Math.floor(width / columns)
                    Repeater {
                        model: view.items
                        delegate: PosterCard {
                            required property var modelData
                            width: flow.cell; height: flow.cell * 1.5 + 54
                            title: modelData.titulo || ""
                            year: modelData.anio || 0
                            subtitle: modelData.rol ? modelData.rol : (modelData.kind === "episodio" ? (modelData.serieTitulo || "Episodio") : "")
                            posterUrl: modelData.posterUrl || ""
                            progressState: modelData.progreso ? modelData.progreso.estado : ""
                            progressFraction: modelData.progreso && modelData.duracionSegundos > 0 ? Math.min(1, modelData.progreso.posicionSegundos / modelData.duracionSegundos) : 0
                            onActivated: view.openItem(modelData)
                            onMenuRequested: (x, y) => view.menuRequested({ id: modelData.id, kind: modelData.kind === "pelicula" ? "movie" : modelData.kind, title: modelData.titulo }, x, y)
                        }
                    }
                }
            }
            Text { id: empty; y: head.height + 40; visible: view.loaded && view.items.length === 0; color: Tokens.textMuted; font.pixelSize: 15
                   text: "No hay títulos de tu colección con esta persona." }
        }
    }
    SmoothWheel { flickable: flick }
}

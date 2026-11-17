import QtQuick
import QtQuick.Effects
import CinemaLove

// Ficha de serie y temporadas (design/11 · P6): héroe, acción «Continuar T2 · E4», temporadas, lista de episodios, reparto y equipo.
Rectangle {
    id: view
    color: Tokens.bg
    property var series: ({})          // mapa de SeriesCatalog.detail()
    property var catalog               // SeriesCatalog
    signal back()
    signal playEpisode(string episodeId, real start)
    signal markRequested(var seriesId, var season, string estado)   // season === null → serie completa

    property int season: 0
    property string lastSeriesId: ""
    property var episodeList: []
    readonly property var next: series.next || ({})
    readonly property var nextEp: next.episode || ({})

    function refreshEpisodes() { episodeList = catalog && series.id ? catalog.episodes(series.id, season) : [] }
    onSeriesChanged: {
        const seasons = series.seasons || []
        const target = series.next && series.next.episode ? series.next.episode.season : undefined
        if (!seasons.some(s => s.number === season)) season = target !== undefined ? target : (seasons.length ? seasons[0].number : 0)
        else if (series.id !== lastSeriesId && target !== undefined) season = target   // otra serie: se abre en la temporada que toca
        lastSeriesId = series.id || ""
        refreshEpisodes()
    }
    onSeasonChanged: refreshEpisodes()

    function dur(sec) { const m = Math.round(sec / 60), h = Math.floor(m / 60); return h > 0 ? h + " h " + (m % 60) + " min" : m + " min" }
    function crewByRole(roles) {
        const out = []
        for (const c of (series.crew || [])) if (roles.some(r => (c.rol || "").indexOf(r) >= 0) && out.indexOf(c.nombre) < 0) out.push(c.nombre)
        return out.slice(0, 6).join(", ")
    }
    function startOf(ep) { return ep.progressState === "enProgreso" ? ep.progressPos : 0 }
    readonly property string primaryLabel: {
        if (!nextEp.id) return "Sin episodios disponibles"
        const t = nextEp.label + " — " + nextEp.title
        switch (next.mode) {
        case "resume": return "▶  Continuar " + t
        case "next": return "▶  Siguiente: " + t
        case "rewatch": return "▶  Volver a ver desde " + nextEp.label
        default: return "▶  Reproducir " + t
        }
    }

    function scrollTo(y) { flick.contentY = y }

    Keys.onEscapePressed: view.back()
    Keys.onBackPressed: view.back()

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.y + body.height + 64
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Item {
            id: hero
            width: parent.width
            height: Math.max(420, view.height * 0.56)
            Image {
                anchors.fill: parent
                source: Images.src(view.series.backdropUrl)
                asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 1280
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 300 } }
            }
            Rectangle { anchors.fill: parent; gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 0.55; color: "#000E0D11" }
                GradientStop { position: 1.0; color: Tokens.bg } } }
            Rectangle { anchors.fill: parent; gradient: Gradient { orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#F20E0D11" }
                GradientStop { position: 0.45; color: "#CC0E0D11" }
                GradientStop { position: 0.85; color: "#330E0D11" }
                GradientStop { position: 1.0; color: "#000E0D11" } } }
        }

        Rectangle {
            id: poster
            x: 56; y: 84; width: 220; height: 330
            radius: Tokens.radius; color: Tokens.surface; border.color: Tokens.border
            Image {
                anchors { fill: parent; margins: 1 }
                source: Images.src(view.series.posterUrl)
                asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 440
                layer.enabled: true
                layer.effect: MultiEffect { maskEnabled: true; maskSource: posterMask; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0 }
            }
            Rectangle { id: posterMask; anchors.fill: parent; radius: Tokens.radius; layer.enabled: true; visible: false }
        }

        Column {
            id: info
            x: poster.x + poster.width + 36; y: 120
            width: Math.min(parent.width - x - 56, 860)
            spacing: 10
            Text { width: parent.width; text: view.series.title || ""; color: Tokens.text; font.pixelSize: 48; font.family: "Georgia"
                   font.weight: Font.DemiBold; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
            Text {
                width: parent.width; elide: Text.ElideRight; color: Tokens.textMuted; font.pixelSize: 15
                text: {
                    const p = [], s = view.series
                    if (s.origTitle && s.origTitle !== s.title) p.push(s.origTitle)
                    if (s.year > 0) p.push(s.year)
                    const n = (s.seasons || []).length
                    p.push(n === 1 ? "1 temporada" : n + " temporadas")
                    p.push(s.episodeCount + " episodios")
                    if (s.watchedCount > 0) p.push(s.watchedCount + " vistos")
                    return p.join("  ·  ")
                }
            }
            Flow {
                width: parent.width; spacing: 8
                Repeater {
                    model: view.series.genres ? view.series.genres.split(" · ") : []
                    delegate: Rectangle {
                        height: 26; width: chipText.width + 24; radius: 13; color: "#24E23F52"; border.color: "#55E23F52"
                        Text { id: chipText; anchors.centerIn: parent; text: modelData; color: Tokens.accentSoft; font.pixelSize: 12; font.weight: Font.Medium }
                    }
                }
                Text { visible: !!view.series.studio; height: 26; verticalAlignment: Text.AlignVCenter; leftPadding: 8; text: view.series.studio || ""; color: Tokens.textFaint; font.pixelSize: 12 }
            }
            Item { width: 1; height: 8 }
            Rectangle {
                id: playBtn
                height: 48; width: Math.min(info.width, playLabel.implicitWidth + 56); radius: Tokens.radius
                opacity: view.nextEp.id ? 1 : 0.4
                color: playHover.hovered && view.nextEp.id ? Tokens.accentSoft : Tokens.accent
                Behavior on color { ColorAnimation { duration: Tokens.durFast } }
                Text { id: playLabel; anchors.centerIn: parent; width: parent.width - 40; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                       color: "white"; font.pixelSize: 15; font.weight: Font.DemiBold; text: view.primaryLabel }
                HoverHandler { id: playHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: !!view.nextEp.id; onTapped: view.playEpisode(view.nextEp.id, view.startOf(view.nextEp)) }
            }
            Row {
                spacing: 10
                readonly property var curSeason: (view.series.seasons || []).find(s => s.number === view.season)
                readonly property bool seasonDone: !!curSeason && curSeason.episodes > 0 && curSeason.watched === curSeason.episodes
                readonly property bool seriesDone: view.series.episodeCount > 0 && view.series.watchedCount === view.series.episodeCount
                AdminButton { text: parent.seasonDone ? "Marcar temporada " + view.season + " como no vista" : "✓  Marcar temporada " + view.season + " como vista"
                    onClicked: view.markRequested(view.series.id, view.season, parent.seasonDone ? "noVisto" : "visto") }
                AdminButton { text: parent.seriesDone ? "Marcar serie como no vista" : "✓  Marcar serie como vista"
                    onClicked: view.markRequested(view.series.id, null, parent.seriesDone ? "noVisto" : "visto") }
            }
            Rectangle {
                visible: view.next.mode === "resume" && view.nextEp.duration > 0
                width: 360; height: 4; radius: 2; color: Tokens.border
                Rectangle { height: parent.height; radius: 2; color: Tokens.accent; width: parent.width * (view.nextEp.progressFraction || 0) }
            }
        }

        Column {
            id: body
            x: 56; y: Math.max(hero.height - 20, poster.y + poster.height + 32)
            width: parent.width - 112
            spacing: 32

            Column {
                width: Math.min(parent.width, 760); spacing: 10; visible: !!view.series.synopsis
                Text { text: "SINOPSIS"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                Text { width: parent.width; text: view.series.synopsis || ""; color: Tokens.text; font.pixelSize: 15; lineHeight: 1.45; wrapMode: Text.WordWrap }
            }

            // --- Temporadas ---
            Column {
                width: parent.width; spacing: 12
                visible: (view.series.seasons || []).length > 0
                Text { text: "TEMPORADAS"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                ListView {
                    width: parent.width; height: 290
                    orientation: ListView.Horizontal; spacing: 16; clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: view.series.seasons || []
                    delegate: Item {
                        required property var modelData
                        readonly property bool selected: modelData.number === view.season
                        width: 140; height: 290
                        Rectangle {
                            id: sFrame
                            width: 140; height: 210; radius: Tokens.radius; color: Tokens.surface
                            border.width: selected || sHover.hovered ? 2 : 1
                            border.color: selected ? Tokens.accent : (sHover.hovered ? Tokens.accentSoft : Tokens.border)
                            Image {
                                anchors { fill: parent; margins: 1 }
                                source: Images.src(modelData.posterUrl); asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 300
                                opacity: selected ? 1 : 0.82
                            }
                            Rectangle { visible: modelData.watched === modelData.episodes && modelData.episodes > 0
                                anchors { right: parent.right; top: parent.top; margins: 8 } width: 24; height: 24; radius: 12; color: "#E64CAF7D"
                                Text { anchors.centerIn: parent; text: "✓"; color: "white"; font.pixelSize: 14; font.bold: true } }
                            Rectangle { visible: modelData.progressFraction > 0 && modelData.progressFraction < 1
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 } height: 4; color: "#99000000"
                                Rectangle { height: parent.height; width: parent.width * modelData.progressFraction; color: Tokens.accent } }
                        }
                        Text { y: 218; width: 140; text: "Temporada " + modelData.number; color: selected ? Tokens.text : Tokens.textMuted; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
                        Text { y: 238; width: 140; text: modelData.episodes + (modelData.episodes === 1 ? " episodio" : " episodios"); color: Tokens.textFaint; font.pixelSize: 12 }
                        HoverHandler { id: sHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: view.season = modelData.number }
                    }
                }
            }

            // --- Episodios de la temporada elegida ---
            Column {
                width: Math.min(parent.width, 980); spacing: 8
                visible: view.episodeList.length > 0
                Text { text: ("TEMPORADA " + view.season + " · EPISODIOS"); color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5; bottomPadding: 4 }
                Repeater {
                    model: view.episodeList
                    delegate: Rectangle {
                        id: epRow
                        required property var modelData
                        readonly property bool isNext: modelData.id === view.nextEp.id
                        width: parent.width; height: 116; radius: Tokens.radius
                        color: rowHover.hovered ? Tokens.surfaceHover : (isNext ? "#14E23F52" : Tokens.surface)
                        border.color: isNext ? "#55E23F52" : Tokens.border
                        Behavior on color { ColorAnimation { duration: Tokens.durFast } }

                        Rectangle {
                            id: still
                            x: 8; y: 8; width: 176; height: 99; radius: Tokens.radiusSmall; color: Tokens.bg; clip: true
                            Image { anchors.fill: parent; source: Images.src(modelData.stillUrl); asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 360 }
                            Text { anchors.centerIn: parent; visible: rowHover.hovered; text: "▶"; color: "white"; font.pixelSize: 28
                                   style: Text.Outline; styleColor: "#99000000" }
                            Rectangle { visible: modelData.progressState === "enProgreso" && modelData.progressFraction > 0
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 4; color: "#99000000"
                                Rectangle { height: parent.height; width: parent.width * modelData.progressFraction; color: Tokens.accent } }
                        }
                        Column {
                            anchors { left: still.right; leftMargin: 16; right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 4
                            Row {
                                spacing: 10; width: parent.width
                                Text { text: modelData.number + ". " + modelData.title; color: Tokens.text; font.pixelSize: 15; font.weight: Font.DemiBold
                                       width: Math.min(implicitWidth, parent.width - metaRow.width - 20); elide: Text.ElideRight }
                                Row { id: metaRow; spacing: 10
                                    Rectangle { visible: epRow.isNext; height: 20; width: nextTxt.width + 14; radius: 10; color: "#33E23F52"; anchors.verticalCenter: parent.verticalCenter
                                        Text { id: nextTxt; anchors.centerIn: parent; text: "Siguiente"; color: Tokens.accentSoft; font.pixelSize: 11; font.weight: Font.DemiBold } }
                                    Text { visible: modelData.duration > 0; text: view.dur(modelData.duration); color: Tokens.textMuted; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                    Text { visible: modelData.progressState === "visto"; text: "✓"; color: "#4CAF7D"; font.pixelSize: 16; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                            Text { width: parent.width; text: modelData.synopsis || "Sin sinopsis."; color: modelData.synopsis ? Tokens.textMuted : Tokens.textFaint
                                   font.pixelSize: 13; lineHeight: 1.3; wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight }
                        }
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: view.playEpisode(modelData.id, view.startOf(modelData)) }
                    }
                }
            }

            Column {
                width: parent.width; spacing: 12
                visible: !!view.series.cast && view.series.cast.length > 0
                Text { text: "REPARTO"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                ListView {
                    width: parent.width; height: 150
                    orientation: ListView.Horizontal; spacing: 20; clip: true; boundsBehavior: Flickable.StopAtBounds
                    model: view.series.cast || []
                    delegate: Item {
                        width: 96; height: 150
                        Rectangle {
                            width: 80; height: 80; radius: 40; x: 8; color: Tokens.surface; border.color: Tokens.border
                            Text { anchors.centerIn: parent; visible: !photo.visible; text: (modelData.nombre || "?").charAt(0); color: Tokens.textFaint; font.pixelSize: 28 }
                            Image {
                                id: photo
                                anchors { fill: parent; margins: 1 }
                                visible: status === Image.Ready
                                source: Images.src(modelData.fotoUrl); asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 160
                                layer.enabled: true
                                layer.effect: MultiEffect { maskEnabled: true; maskSource: avMask; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0 }
                            }
                            Rectangle { id: avMask; anchors.fill: parent; radius: width / 2; layer.enabled: true; visible: false }
                        }
                        Text { y: 88; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: modelData.nombre || ""; color: Tokens.text; font.pixelSize: 12; font.weight: Font.Medium; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                        Text { y: 120; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: modelData.rol || ""; color: Tokens.textMuted; font.pixelSize: 11; elide: Text.ElideRight }
                    }
                }
            }
            Column {
                width: Math.min(parent.width, 760); spacing: 8
                visible: view.crewByRole(["Creador", "Creator", "Productor ejecutivo", "Director"]) !== ""
                Text { text: "EQUIPO"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14
                       text: "Creación y producción: " + view.crewByRole(["Creador", "Creator", "Productor ejecutivo", "Director"]) }
            }
        }
    }
}

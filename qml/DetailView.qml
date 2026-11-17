import QtQuick
import QtQuick.Effects
import CinemaLove

// Ficha de película (design/11 · P5), versión del spike: backdrop, póster, metadatos, acción principal, sinopsis, reparto y equipo.
Rectangle {
    id: view
    color: Tokens.bg
    property var movie: ({})
    signal back()
    signal play(real start)

    readonly property bool inProgress: movie.progressState === "enProgreso" && movie.progressPos > 0
    readonly property bool watched: movie.progressState === "visto"
    readonly property bool available: !!movie.available

    function duration(sec) {
        const m = Math.round(sec / 60), h = Math.floor(m / 60)
        return h > 0 ? h + " h " + (m % 60) + " min" : m + " min"
    }
    function crewByRole(roles) {
        const out = []
        for (const c of (movie.crew || [])) if (roles.indexOf(c.rol) >= 0 && out.indexOf(c.nombre) < 0) out.push(c.nombre)
        return out.join(", ")
    }

    Keys.onEscapePressed: view.back()
    Keys.onBackPressed: view.back()

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.y + body.height + 64
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // --- Backdrop a sangre con degradados (design/10 §1) ---
        Item {
            id: hero
            width: parent.width
            height: Math.max(420, view.height * 0.56)
            Image {
                anchors.fill: parent
                source: Images.src(view.movie.backdropUrl)
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 1280
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 300 } }
            }
            Rectangle { anchors.fill: parent; gradient: Gradient { // inferior
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 0.55; color: "#000E0D11" }
                GradientStop { position: 1.0; color: Tokens.bg } } }
            Rectangle { anchors.fill: parent; gradient: Gradient { // lateral izquierdo
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#F20E0D11" }
                GradientStop { position: 0.45; color: "#CC0E0D11" }
                GradientStop { position: 0.85; color: "#330E0D11" }
                GradientStop { position: 1.0; color: "#000E0D11" } } }
        }

        // --- Póster + datos principales ---
        Rectangle {
            id: poster
            x: 56; y: 84
            width: 220; height: 330
            radius: Tokens.radius; color: Tokens.surface
            border.color: Tokens.border
            Image {
                anchors { fill: parent; margins: 1 }
                source: Images.src(view.movie.posterUrl)
                asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 440
                layer.enabled: true
                layer.effect: MultiEffect { maskEnabled: true; maskSource: posterMask; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0 }
            }
            Rectangle { id: posterMask; anchors.fill: parent; radius: Tokens.radius; layer.enabled: true; visible: false }
        }

        Column {
            id: info
            x: poster.x + poster.width + 36
            y: 120
            width: Math.min(parent.width - x - 56, 820)
            spacing: 10

            Text {
                width: parent.width
                text: view.movie.title || ""
                color: Tokens.text; font.pixelSize: 48; font.family: "Georgia"; font.weight: Font.DemiBold
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            }
            Text {
                width: parent.width
                color: Tokens.textMuted; font.pixelSize: 15
                elide: Text.ElideRight
                text: {
                    const p = []
                    if (view.movie.origTitle && view.movie.origTitle !== view.movie.title) p.push(view.movie.origTitle)
                    if (view.movie.year > 0) p.push(view.movie.year)
                    if (view.movie.duration > 0) p.push(view.duration(view.movie.duration))
                    if (view.movie.lang) p.push(view.movie.lang.toUpperCase())
                    return p.join("  ·  ")
                }
            }
            Flow {
                width: parent.width; spacing: 8
                Repeater {
                    model: view.movie.genres ? view.movie.genres.split(" · ") : []
                    delegate: Rectangle {
                        height: 26; width: chipText.width + 24; radius: 13
                        color: "#24E23F52"; border.color: "#55E23F52"
                        Text { id: chipText; anchors.centerIn: parent; text: modelData; color: Tokens.accentSoft; font.pixelSize: 12; font.weight: Font.Medium }
                    }
                }
                Text { visible: !!view.movie.studio; height: 26; verticalAlignment: Text.AlignVCenter; leftPadding: 8; text: view.movie.studio || ""; color: Tokens.textFaint; font.pixelSize: 12 }
            }

            Item { width: 1; height: 8 }

            Row {
                spacing: 12
                Rectangle { // acción primaria
                    id: playBtn
                    height: 48; width: playLabel.width + 56; radius: Tokens.radius
                    opacity: view.available ? 1 : 0.4
                    color: playHover.hovered && view.available ? Tokens.accentSoft : Tokens.accent
                    Behavior on color { ColorAnimation { duration: Tokens.durFast } }
                    border.width: playBtn.activeFocus ? 2 : 0; border.color: "white"
                    activeFocusOnTab: true
                    Text {
                        id: playLabel; anchors.centerIn: parent; color: "white"; font.pixelSize: 15; font.weight: Font.DemiBold
                        text: !view.available ? "Archivo no disponible"
                              : view.inProgress ? "▶  Continuar · " + view.duration(Math.max(0, view.movie.duration - view.movie.progressPos)) + " restantes"
                              : view.watched ? "▶  Volver a ver" : "▶  Reproducir"
                    }
                    HoverHandler { id: playHover; cursorShape: view.available ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    TapHandler { enabled: view.available; onTapped: view.play(view.inProgress ? view.movie.progressPos : 0) }
                    Keys.onReturnPressed: if (view.available) view.play(view.inProgress ? view.movie.progressPos : 0)
                }
                Rectangle { // reiniciar desde el principio
                    visible: view.inProgress && view.available
                    height: 48; width: 48; radius: Tokens.radius
                    color: restartHover.hovered ? Tokens.surfaceHover : Tokens.surface; border.color: Tokens.border
                    Text { anchors.centerIn: parent; text: "↺"; color: Tokens.text; font.pixelSize: 22 }
                    HoverHandler { id: restartHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: view.play(0) }
                }
                Rectangle {
                    visible: view.watched
                    height: 48; width: watchedLabel.width + 32; radius: Tokens.radius
                    color: "#1F4CAF7D"; border.color: "#664CAF7D"
                    Text { id: watchedLabel; anchors.centerIn: parent; text: "✓  Vista"; color: "#4CAF7D"; font.pixelSize: 14; font.weight: Font.Medium }
                }
            }
            Rectangle { // barra de progreso
                visible: view.inProgress && view.movie.duration > 0
                width: 360; height: 4; radius: 2; color: Tokens.border
                Rectangle { height: parent.height; radius: 2; color: Tokens.accent
                    width: parent.width * Math.min(1, view.movie.progressPos / Math.max(1, view.movie.duration)) }
            }
        }

        // --- Cuerpo: sinopsis, reparto, equipo ---
        Column {
            id: body
            x: 56; y: Math.max(hero.height - 20, poster.y + poster.height + 32)
            width: parent.width - 112
            spacing: 32

            Column {
                width: Math.min(parent.width, 760); spacing: 10
                visible: !!view.movie.synopsis
                Text { text: "SINOPSIS"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                Text { width: parent.width; text: view.movie.synopsis || ""; color: Tokens.text; font.pixelSize: 15; lineHeight: 1.45; wrapMode: Text.WordWrap }
            }
            Column {
                width: parent.width; spacing: 12
                visible: !!view.movie.cast && view.movie.cast.length > 0
                Text { text: "REPARTO"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                ListView {
                    width: parent.width; height: 150
                    orientation: ListView.Horizontal; spacing: 20; clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: view.movie.cast || []
                    delegate: Item {
                        width: 96; height: 150
                        Rectangle {
                            id: avatar
                            width: 80; height: 80; radius: 40; x: 8
                            color: Tokens.surface; border.color: Tokens.border
                            Text { anchors.centerIn: parent; visible: !photo.visible; text: (modelData.nombre || "?").charAt(0); color: Tokens.textFaint; font.pixelSize: 28 }
                            Image {
                                id: photo
                                anchors { fill: parent; margins: 1 }
                                visible: status === Image.Ready
                                source: Images.src(modelData.fotoUrl)
                                asynchronous: true; fillMode: Image.PreserveAspectCrop; sourceSize.width: 160
                                layer.enabled: true
                                layer.effect: MultiEffect { maskEnabled: true; maskSource: avatarMask; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0 }
                            }
                            Rectangle { id: avatarMask; anchors.fill: parent; radius: width / 2; layer.enabled: true; visible: false }
                        }
                        Text { y: 88; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: modelData.nombre || ""; color: Tokens.text; font.pixelSize: 12; font.weight: Font.Medium; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                        Text { y: 120; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: modelData.rol || ""; color: Tokens.textMuted; font.pixelSize: 11; elide: Text.ElideRight }
                    }
                }
            }
            Column {
                width: Math.min(parent.width, 760); spacing: 8
                visible: view.crewByRole(["Director"]) !== "" || view.crewByRole(["Guionista", "Screenplay", "Writer"]) !== ""
                Text { text: "EQUIPO"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                Text { visible: text !== ""; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14
                       text: view.crewByRole(["Director"]) ? "Dirección: " + view.crewByRole(["Director"]) : "" }
                Text { visible: text !== ""; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14
                       text: view.crewByRole(["Guionista", "Screenplay", "Writer"]) ? "Guion: " + view.crewByRole(["Guionista", "Screenplay", "Writer"]) : "" }
            }
        }
    }
}

import QtQuick
import CinemaLove

// Spike de la Fase 0: reproduce con mpv y superpone controles QML sobre el video.
Window {
    id: root
    width: 1280
    height: 720
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "CinemaLove · spike Qt 6 + libmpv"

    color: Tokens.bg
    onClosing: player.shutdown()

    property bool controlsVisible: true
    property bool playing: false          // true cuando se está viendo una película (si no, se muestra el catálogo)
    property string movieTitle: ""
    property string notice: ""

    MovieModel { id: movies; dbPath: spikeArgs.db }

    function openMovie(m) {
        if (!movies.fileExists(m.path)) {
            notice = "No se encuentra el archivo de «" + m.title + "». ¿Está conectado el disco " + m.path.substring(0, 2) + "?"
            noticeTimer.restart()
            return
        }
        movieTitle = m.title + (m.year > 0 ? " (" + m.year + ")" : "")
        playing = true
        controlsVisible = true
        player.load(m.path)
        hideTimer.restart()
        player.forceActiveFocus()
    }
    function closeMovie() {
        player.stop()
        if (visibility === Window.FullScreen) visibility = Window.Windowed
        playing = false
        movieTitle = ""
        hideTimer.stop()
    }

    function fmt(s) {
        if (!isFinite(s) || s < 0) s = 0
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = Math.floor(s % 60)
        const mm = (h > 0 && m < 10 ? "0" : "") + m, ss = (sec < 10 ? "0" : "") + sec
        return (h > 0 ? h + ":" : "") + mm + ":" + ss
    }
    function trackLabel(list, id) {
        if (id === "no") return "Desactivado"
        for (const t of list) if (t.id === id) return (t.lang || "?") + (t.title ? " · " + t.title : "") + " (" + t.codec + ")"
        return id
    }
    function cycle(list, current, includeOff) {
        const ids = list.map(t => t.id)
        if (includeOff) ids.push("no")
        if (ids.length === 0) return ""
        const i = ids.indexOf(current)
        return ids[(i + 1) % ids.length]
    }

    MpvItem {
        id: player
        anchors.fill: parent
        focus: true
        onEndOfFile: (reason) => { if (reason === "eof" && !spikeArgs.selftest) root.closeMovie() }

        Keys.onPressed: (e) => {
            switch (e.key) {
            case Qt.Key_Space: togglePause(); break
            case Qt.Key_Right: seek(e.modifiers & Qt.ShiftModifier ? 30 : 10, true); break
            case Qt.Key_Left: seek(e.modifiers & Qt.ShiftModifier ? -30 : -10, true); break
            case Qt.Key_Up: volume = volume + 5; break
            case Qt.Key_Down: volume = volume - 5; break
            case Qt.Key_F: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen; break
            case Qt.Key_Escape: if (root.visibility === Window.FullScreen) root.visibility = Window.Windowed; else root.closeMovie(); break
            case Qt.Key_Backspace: root.closeMovie(); break
            case Qt.Key_A: { const n = root.cycle(audioTracks, audioId, false); if (n) setAudio(n); break }
            case Qt.Key_S: { const n = root.cycle(subtitleTracks, subtitleId, true); if (n) setSubtitle(n); break }
            default: return
            }
            root.controlsVisible = true
            hideTimer.restart()
            e.accepted = true
        }

        MouseArea {
            anchors.fill: parent
            onClicked: { root.controlsVisible = !root.controlsVisible; hideTimer.restart() }
            onDoubleClicked: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
        }
    }

    CatalogView {
        id: catalog
        anchors.fill: parent
        visible: !root.playing
        model: movies
        total: movies.total
        error: movies.error
        onSearch: (t) => movies.filter = t
        onMovieChosen: (m) => root.openMovie(m)
    }

    Rectangle { // aviso temporal (archivo no disponible)
        visible: root.notice !== "" && !root.playing
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 32 }
        width: Math.min(parent.width - 64, noticeText.implicitWidth + 40); height: 44; radius: Tokens.radius
        color: Tokens.surfaceRaised; border.color: Tokens.danger
        Text { id: noticeText; anchors.centerIn: parent; width: parent.width - 40; elide: Text.ElideRight; text: root.notice; color: Tokens.text; font.pixelSize: 14 }
    }
    Timer { id: noticeTimer; interval: 5000; onTriggered: root.notice = "" }

    Timer { id: hideTimer; interval: 30000; onTriggered: root.controlsVisible = false }

    // --- Barra de título propia (ventana sin marco) ---
    readonly property bool fullscreen: root.visibility === Window.FullScreen
    Rectangle {
        id: titleBar
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 36
        visible: (!root.playing || root.controlsVisible) && !root.fullscreen
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.playing ? "#E6000000" : Tokens.bg }
            GradientStop { position: 1.0; color: root.playing ? "#00000000" : Tokens.bg }
        }
        Row {
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            spacing: 12
            Text {
                visible: root.playing
                text: "‹ Biblioteca"; color: backHover.hovered ? "white" : "#bbbbbb"; font.pixelSize: 13
                HoverHandler { id: backHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.closeMovie() }
            }
            Text { text: root.playing ? root.movieTitle : "CinemaLove"; color: "#dddddd"; font.pixelSize: 13; font.bold: true }
        }
        DragHandler { target: null; onActiveChanged: if (active) root.startSystemMove() }
        TapHandler { acceptedButtons: Qt.LeftButton; onDoubleTapped: root.visibility = root.visibility === Window.Maximized ? Window.Windowed : Window.Maximized }
        Row {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            Repeater {
                model: [ { t: "–", a: "min" }, { t: root.visibility === Window.Maximized ? "❐" : "□", a: "max" }, { t: "✕", a: "close" } ]
                delegate: Rectangle {
                    width: 46; height: titleBar.height
                    color: ma.containsMouse ? (modelData.a === "close" ? "#e81123" : "#33ffffff") : "transparent"
                    Text { anchors.centerIn: parent; text: modelData.t; color: "white"; font.pixelSize: 14 }
                    MouseArea {
                        id: ma; anchors.fill: parent; hoverEnabled: true
                        onClicked: {
                            if (modelData.a === "min") root.showMinimized()
                            else if (modelData.a === "max") root.visibility = root.visibility === Window.Maximized ? Window.Windowed : Window.Maximized
                            else root.close()
                        }
                    }
                }
            }
        }
    }

    // Bordes de redimensionado (la ventana sin marco no los trae)
    Repeater {
        model: [
            { e: Qt.LeftEdge, c: Qt.SizeHorCursor, l: true, t: false, r: false, b: false },
            { e: Qt.RightEdge, c: Qt.SizeHorCursor, l: false, t: false, r: true, b: false },
            { e: Qt.TopEdge, c: Qt.SizeVerCursor, l: false, t: true, r: false, b: false },
            { e: Qt.BottomEdge, c: Qt.SizeVerCursor, l: false, t: false, r: false, b: true },
            { e: Qt.LeftEdge | Qt.TopEdge, c: Qt.SizeFDiagCursor, l: true, t: true, r: false, b: false },
            { e: Qt.RightEdge | Qt.BottomEdge, c: Qt.SizeFDiagCursor, l: false, t: false, r: true, b: true },
            { e: Qt.RightEdge | Qt.TopEdge, c: Qt.SizeBDiagCursor, l: false, t: true, r: true, b: false },
            { e: Qt.LeftEdge | Qt.BottomEdge, c: Qt.SizeBDiagCursor, l: true, t: false, r: false, b: true }
        ]
        delegate: MouseArea {
            enabled: !root.fullscreen && root.visibility !== Window.Maximized
            readonly property bool corner: (modelData.l || modelData.r) && (modelData.t || modelData.b)
            x: modelData.r ? root.width - width : 0
            y: modelData.b ? root.height - height : 0
            width: modelData.l || modelData.r ? 6 : root.width
            height: modelData.t || modelData.b ? 6 : root.height
            cursorShape: modelData.c
            onPressed: root.startSystemResize(modelData.e)
        }
    }

    // --- Superposición de controles (demuestra que QML se dibuja encima del video) ---
    Rectangle {
        id: bar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 130
        visible: root.playing && root.controlsVisible
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: "#E6000000" }
        }

        Text {
            id: stateText
            anchors { left: parent.left; leftMargin: 24; top: parent.top; topMargin: 18 }
            color: "#cccccc"; font.pixelSize: 13
            text: player.state + (player.buffering ? " · cargando buffer…" : "") + "  |  " + player.info
        }

        // Línea de tiempo
        Rectangle {
            id: track
            anchors { left: parent.left; right: parent.right; leftMargin: 24; rightMargin: 24; bottom: parent.bottom; bottomMargin: 58 }
            height: 6; radius: 3; color: "#44ffffff"
            Rectangle {
                height: parent.height; radius: 3; color: "#e50914"
                width: player.duration > 0 ? parent.width * Math.min(1, player.position / player.duration) : 0
            }
            MouseArea {
                anchors { fill: parent; topMargin: -10; bottomMargin: -10 }
                onClicked: (m) => { if (player.duration > 0) player.seek(player.duration * m.x / width, false) }
            }
        }
        Text {
            anchors { left: track.left; top: track.bottom; topMargin: 8 }
            color: "white"; font.pixelSize: 15
            text: root.fmt(player.position) + " / " + root.fmt(player.duration)
        }
        Text {
            anchors { right: track.right; top: track.bottom; topMargin: 8 }
            color: "#dddddd"; font.pixelSize: 13
            text: "Audio [A]: " + root.trackLabel(player.audioTracks, player.audioId)
                  + "   Subs [S]: " + root.trackLabel(player.subtitleTracks, player.subtitleId)
                  + "   Vol [↑↓]: " + Math.round(player.volume) + " %"
        }
        Text {
            anchors { horizontalCenter: parent.horizontalCenter; top: track.bottom; topMargin: 8 }
            visible: root.width >= 1400
            color: "#999999"; font.pixelSize: 12
            text: "Espacio pausa · ←/→ ±10 s (Shift ±30) · F pantalla completa · clic muestra/oculta"
        }
    }

    // --- Autoprueba: secuencia automática y volcado de métricas ---
    property var selfLog: []
    function mark(msg) { selfLog.push({ t: player.position, msg: msg }) }

    Timer { id: t1; interval: 3000; onTriggered: { root.mark("seek a 30 s"); player.seek(30, false) } }
    Timer { id: t2; interval: 6000; onTriggered: { root.mark("seek -10 s"); player.seek(-10, true) } }
    Timer { id: t3; interval: 8000; onTriggered: { root.mark("siguiente audio"); const n = root.cycle(player.audioTracks, player.audioId, false); if (n) player.setAudio(n) } }
    Timer { id: t4; interval: 9000; onTriggered: { root.mark("siguiente subtítulo"); const n = root.cycle(player.subtitleTracks, player.subtitleId, true); if (n) player.setSubtitle(n) } }
    Timer { id: t4b; interval: 10000; onTriggered: { root.mark("captura"); player.grabWindowTo((spikeArgs.statsOut || "selftest") + ".png") } }
    Timer { id: t5; interval: 11000; onTriggered: { root.mark("pausa"); player.togglePause() } }
    Timer { id: t6; interval: 13000; onTriggered: { root.mark("reanuda"); player.togglePause() } }
    Timer { id: t7; interval: 14000; onTriggered: { root.mark("volumen 150"); player.volume = 150 } }
    Timer { id: t7a; interval: 15000; onTriggered: { root.mark("pantalla completa"); root.visibility = Window.FullScreen } }
    Timer { id: t7b; interval: 16500; onTriggered: { root.mark("captura pantalla completa"); player.grabWindowTo((spikeArgs.statsOut || "selftest") + "-fs.png") } }
    Timer { id: t7c; interval: 17000; onTriggered: { root.mark("ventana"); root.visibility = Window.Windowed } }
    Timer { id: t7d; interval: 18500; onTriggered: { root.mark("redimensiona 900x520"); root.width = 900; root.height = 520 } }
    Timer { id: t7e; interval: 20000; onTriggered: { root.mark("captura redimensionada"); player.grabWindowTo((spikeArgs.statsOut || "selftest") + "-small.png") } }
    Timer { id: t8; interval: 21000; onTriggered: {
        player.writeStats(spikeArgs.statsOut || "selftest-stats.json", { selftest: root.selfLog, ventana: root.width + "x" + root.height, sinMarco: (root.flags & Qt.FramelessWindowHint) !== 0 })
        player.shutdown()
        Qt.quit()
    } }

    Timer { id: openTimer; interval: 3000; onTriggered: root.openMovie(movies.get(0)) }
    Timer { id: shotTimer; interval: spikeArgs.openFirst ? 14000 : 9000; onTriggered: {
        player.grabWindowTo(spikeArgs.catalogShot); player.shutdown(); Qt.quit() } }

    Component.onCompleted: {
        if (spikeArgs.catalogShot) { if (spikeArgs.search) catalog.setSearch(spikeArgs.search); shotTimer.start(); if (spikeArgs.openFirst) openTimer.start() }
        if (spikeArgs.file) {
            root.playing = true
            root.movieTitle = spikeArgs.file
            player.load(spikeArgs.file)
            if (spikeArgs.sub) player.addSubtitle(spikeArgs.sub)
        }
        hideTimer.start()
        if (spikeArgs.selftest) {
            root.controlsVisible = true
            hideTimer.stop()
            t1.start(); t2.start(); t3.start(); t4.start(); t4b.start(); t5.start(); t6.start(); t7.start(); t7a.start(); t7b.start(); t7c.start(); t7d.start(); t7e.start(); t8.start()
        }
    }
}

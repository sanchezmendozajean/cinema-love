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
    color: "black"
    title: "CinemaLove · spike Qt 6 + libmpv"

    onClosing: player.shutdown()

    property bool controlsVisible: true

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

        Keys.onPressed: (e) => {
            switch (e.key) {
            case Qt.Key_Space: togglePause(); break
            case Qt.Key_Right: seek(e.modifiers & Qt.ShiftModifier ? 30 : 10, true); break
            case Qt.Key_Left: seek(e.modifiers & Qt.ShiftModifier ? -30 : -10, true); break
            case Qt.Key_Up: volume = volume + 5; break
            case Qt.Key_Down: volume = volume - 5; break
            case Qt.Key_F: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen; break
            case Qt.Key_Escape: if (root.visibility === Window.FullScreen) root.visibility = Window.Windowed; break
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

    Timer { id: hideTimer; interval: 30000; onTriggered: root.controlsVisible = false }

    // --- Superposición de controles (demuestra que QML se dibuja encima del video) ---
    Rectangle {
        id: bar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 130
        visible: root.controlsVisible
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
    Timer { id: t8; interval: 18000; onTriggered: {
        player.writeStats(spikeArgs.statsOut || "selftest-stats.json", { selftest: root.selfLog, ventana: root.width + "x" + root.height })
        player.shutdown()
        Qt.quit()
    } }

    Component.onCompleted: {
        if (spikeArgs.file) {
            player.load(spikeArgs.file)
            if (spikeArgs.sub) player.addSubtitle(spikeArgs.sub)
        }
        hideTimer.start()
        if (spikeArgs.selftest) {
            root.controlsVisible = true
            hideTimer.stop()
            t1.start(); t2.start(); t3.start(); t4.start(); t4b.start(); t5.start(); t6.start(); t7.start(); t8.start()
        }
    }
}

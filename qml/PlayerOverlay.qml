import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Interfaz del reproductor (specs/heredadas/05 §5, adaptada a mpv): barra de controles, OSD, indicador de carga,
// pantalla de error, fin de reproducción, tarjeta «Siguiente episodio», aviso de Esc, selectores de audio/subtítulos,
// configuración de subtítulos y panel de información técnica. Todo son Items QML encima del MpvItem.
Item {
    id: ov
    property var player
    property var thumbs                     // Thumbnailer: fotogramas de vista previa de la línea de tiempo (null = sin vista previa)
    property string title: ""
    property bool isEpisode: false
    property bool hasNext: false
    property string nextTitle: ""
    property string backdropUrl: ""
    property bool fullscreen: false
    property bool controlsVisible: true
    property bool ended: false              // la reproducción terminó y se muestra la pantalla final
    property bool nextCancelled: false      // el usuario canceló el salto automático al siguiente episodio
    property bool exitArmed: false          // primera pulsación de Esc dada
    property bool showRemaining: false
    property var api                        // ApiClient del backend local
    property bool shared: false             // Modo Local: el estilo de subtítulos vive en la base (claves heredadas); en Modo Cliente, solo en este equipo

    signal closeRequested()
    signal nextRequested()
    signal fullscreenRequested()
    signal interacted()                     // cualquier uso de la barra reinicia el temporizador de ocultado
    signal retryRequested()
    signal replayRequested()
    signal seeked()                         // seek o cambio de pista: Main guarda el progreso
    signal focusRequested()                 // al cerrarse un menú, el foco vuelve al video

    readonly property bool failed: player && player.state === "error"
    readonly property real remaining: player ? Math.max(0, player.duration - player.position) : 0
    readonly property bool nextCardShown: isEpisode && hasNext && !nextCancelled && !ended && !failed
                                          && player && player.duration > 0 && remaining <= 20
    property int countdown: 10

    function reset() { nextCancelled = false; countdown = 10; exitArmed = false; osdTimer.stop(); volTimer.stop() }
    function fmt(s) {
        if (!isFinite(s) || s < 0) s = 0
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = Math.floor(s % 60)
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (sec < 10 ? "0" : "") + sec
    }
    function trackLabel(t) {
        const parts = []
        parts.push(t.lang ? t.lang.toUpperCase() : "Desconocido")
        if (t.title) parts.push(t.title)
        if (t.codec) parts.push(t.codec)
        if (t.external) parts.push("externo")
        return parts.join(" · ")
    }

    // --- OSD ---
    property string osdIcon: "play"
    property string osdText: ""
    property int skipAccum: 0
    function flashCenter(icon, text) { osdIcon = icon; osdText = text || ""; osdTimer.restart() }
    function flashSkip(sec) {
        if (osdTimer.running && skipAccum !== 0 && (skipAccum > 0) === (sec > 0)) skipAccum += sec; else skipAccum = sec
        flashCenter(sec > 0 ? "fwd10" : "back10", (skipAccum > 0 ? "+" : "−") + Math.abs(skipAccum) + " s")
    }
    function flashVolume() { volTimer.restart() }
    function adjustVolume(delta) { player.volume = player.volume + delta; flashVolume() }
    function armExit() { exitArmed = true; exitTimer.restart() }
    Timer { id: osdTimer; interval: 1050; onTriggered: ov.skipAccum = 0 }
    Timer { id: volTimer; interval: 1800 }
    Timer { id: exitTimer; interval: 3000; onTriggered: ov.exitArmed = false }
    Timer { // cuenta atrás del siguiente episodio
        interval: 1000; repeat: true; running: ov.nextCardShown && !ov.player.paused
        onTriggered: { ov.countdown -= 1; if (ov.countdown <= 0) ov.nextRequested() }
    }
    onNextCardShownChanged: countdown = 10

    // --- Preferencias locales (volumen, estilo de subtítulos) ---
    property real subSize: 55
    property real subPos: 100
    Component.onCompleted: {
        if (!player) return
        player.volume = Number(player.setting("volume", 100))
        subSize = Number(player.setting("subSize", 55)); subPos = Number(player.setting("subPos", 100))
        player.setSubtitleStyle(subSize, subPos)
        loadShared()
    }
    // Equivalencias con las claves heredadas de Flutter (tamaño 32 y separación 24 por defecto) ↔ mpv (sub-font-size 55, sub-pos 100).
    function fromShared(d) { subSize = Math.round(d.tamano * 55 / 32); subPos = Math.max(40, Math.min(100, Math.round(100 - (d.posicion - 24) / 7.2))); player.setSubtitleStyle(subSize, subPos) }
    function saveSubStyle() {
        player.setSetting("subSize", subSize); player.setSetting("subPos", subPos)
        if (shared && api && api.ready) sharedSave.restart()
    }
    property bool sharedLoaded: false
    function loadShared() {
        if (!shared || sharedLoaded || !api || !api.ready) return
        sharedLoaded = true
        api.request("GET", "/api/v1/admin/ajustes/subtitulos", null, (st, d) => { if (st === 200 && d) fromShared(d) })
    }
    onSharedChanged: { sharedLoaded = false; loadShared() }
    Connections { target: ov.api; function onConfigChanged() { ov.loadShared() } }
    Timer { id: sharedSave; interval: 700; onTriggered: ov.api.request("PUT", "/api/v1/admin/ajustes/subtitulos",
        { tamano: Math.round(ov.subSize * 32 / 55), posicion: Math.round(24 + (100 - ov.subPos) * 7.2) }, () => {}) }
    Timer { id: saveVol; interval: 600; onTriggered: ov.player.setSetting("volume", Math.round(ov.player.volume)) }
    Connections { target: ov.player; function onVolumeChanged() { saveVol.restart() } }

    // Componentes internos
    component IconBtn: Rectangle {
        id: b
        property string icon
        property string tip
        property bool active: false
        signal clicked()
        width: 40; height: 40; radius: 20
        color: hov.hovered ? "#40FFFFFF" : (active ? "#26FFFFFF" : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }
        NavIcon { anchors.centerIn: parent; width: 24; height: 24; name: b.icon; color: "white"; lineWidth: 1.9 }
        HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: { ov.interacted(); b.clicked() } }
        ToolTip.visible: hov.hovered && tip !== ""; ToolTip.text: tip; ToolTip.delay: 600
        Accessible.role: Accessible.Button; Accessible.name: tip
    }
    component FlatSlider: Item {
        id: s
        property real from: 0
        property real to: 100
        property real value: 0
        property real mark: -1               // marca fija (p. ej. el 100 % del volumen)
        signal moved(real v)
        implicitHeight: 24
        readonly property real frac: Math.max(0, Math.min(1, (value - from) / (to - from)))
        Rectangle { id: rail; anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter } height: 4; radius: 2; color: "#44FFFFFF"
            Rectangle { width: parent.width * s.frac; height: parent.height; radius: 2; color: "white" } }
        Rectangle { visible: s.mark >= 0; x: rail.width * (s.mark - s.from) / (s.to - s.from) - 1; y: (s.height - 10) / 2; width: 2; height: 10; color: "#88FFFFFF" }
        Rectangle { x: rail.width * s.frac - 7; anchors.verticalCenter: parent.verticalCenter; width: 14; height: 14; radius: 7; color: "white"
            scale: sm.containsMouse || sm.pressed ? 1.15 : 1; Behavior on scale { NumberAnimation { duration: 100 } } }
        MouseArea {
            id: sm; anchors { fill: parent; topMargin: -6; bottomMargin: -6 } hoverEnabled: true; cursorShape: Qt.PointingHandCursor
            function setFrom(x) { s.moved(s.from + (s.to - s.from) * Math.max(0, Math.min(1, x / width))) }
            onPressed: (m) => { ov.interacted(); setFrom(m.x) }
            onPositionChanged: (m) => { if (pressed) { ov.interacted(); setFrom(m.x) } }
        }
    }

    // ============================ Barra de controles ============================
    Rectangle {
        id: bar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 168
        visible: opacity > 0
        opacity: ov.controlsVisible && !ov.ended && !ov.failed ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 0.55; color: "#B0000000" }
            GradientStop { position: 1.0; color: "#E6000000" }
        }
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true
                    onClicked: ov.interacted(); onWheel: (w) => ov.adjustVolume(w.angleDelta.y > 0 ? 5 : -5) }

        // Línea de tiempo: progreso, buffer, miniatura de tiempo al pasar el cursor y arrastre
        Item {
            id: timeline
            anchors { left: parent.left; right: parent.right; leftMargin: 28; rightMargin: 28; bottom: parent.bottom; bottomMargin: 66 }
            height: 22
            property bool dragging: false
            property real dragFrac: 0
            property real hoverFrac: 0
            readonly property real duration: ov.player ? ov.player.duration : 0
            readonly property real playFrac: duration > 0 ? Math.min(1, ov.player.position / duration) : 0
            readonly property real shownFrac: dragging ? dragFrac : playFrac
            readonly property real bufFrac: duration > 0 ? Math.min(1, Math.max(playFrac, ov.player.cached / duration)) : 0
            Rectangle { id: rail; anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                height: tlMouse.containsMouse || timeline.dragging ? 6 : 4; radius: 3; color: "#44FFFFFF"
                Behavior on height { NumberAnimation { duration: 100 } }
                Rectangle { width: parent.width * timeline.bufFrac; height: parent.height; radius: 3; color: "#66FFFFFF" }
                Rectangle { width: parent.width * timeline.shownFrac; height: parent.height; radius: 3; color: Tokens.accent }
            }
            Rectangle { // tirador
                x: rail.width * timeline.shownFrac - 7; anchors.verticalCenter: parent.verticalCenter
                width: 14; height: 14; radius: 7; color: Tokens.accent; border.color: "white"; border.width: 1.5
                opacity: tlMouse.containsMouse || timeline.dragging ? 1 : 0; Behavior on opacity { NumberAnimation { duration: 100 } }
            }
            // Vista previa: fotograma (si ya se generó) y tiempo bajo el cursor. El fotograma anterior se mantiene hasta que llega el nuevo.
            readonly property bool active: tlMouse.containsMouse || dragging
            readonly property real hoverTime: duration * (dragging ? dragFrac : hoverFrac)
            readonly property real curFrac: dragging ? dragFrac : hoverFrac
            property string thumbSrc: ""
            property real thumbFrac: 0          // posición (0–1) para la que se generó el fotograma mostrado
            // El fotograma solo se muestra mientras el cursor siga cerca de su posición (≤ 10 px): si te alejas más, desaparece
            // y vuelve a aparecer cuando llega el de la posición nueva, en vez de arrastrar un fotograma que ya no corresponde.
            readonly property bool thumbFresh: thumbSrc !== "" && Math.abs(curFrac - thumbFrac) * width <= 10
            onActiveChanged: if (!active) thumbSrc = ""
            function refreshThumb() {
                if (!ov.thumbs || !active) return
                const u = ov.thumbs.urlFor(hoverTime)
                if (u !== "") { thumbSrc = u; thumbFrac = curFrac }
            }
            function askThumb() { if (ov.thumbs && !thumbTimer.running) thumbTimer.start() }
            Timer { id: thumbTimer; interval: 120; onTriggered: if (ov.thumbs && timeline.active) ov.thumbs.request(timeline.hoverTime) }   // como mucho una petición cada 120 ms
            Connections { target: ov.thumbs; function onReady() { timeline.refreshThumb() } }
            Item {
                id: preview
                visible: timeline.active && timeline.duration > 0
                readonly property bool hasThumb: timeline.thumbFresh
                width: hasThumb ? 240 : hoverLabel.width + 16
                height: (hasThumb ? thumbFrame.height + 6 : 0) + 26
                x: Math.max(0, Math.min(timeline.width - width, timeline.width * (timeline.dragging ? timeline.dragFrac : timeline.hoverFrac) - width / 2))
                y: -height - 8
                Rectangle {
                    id: thumbFrame
                    visible: preview.hasThumb
                    width: 240; height: Math.round(240 * (thumbImg.implicitWidth > 0 ? thumbImg.implicitHeight / thumbImg.implicitWidth : 9 / 16))
                    radius: 4; color: "black"; border.color: "#66FFFFFF"; border.width: 1
                    Image { id: thumbImg; anchors { fill: parent; margins: 1 } source: timeline.thumbSrc; cache: false; smooth: true; fillMode: Image.Stretch }
                }
                Rectangle { // tiempo
                    anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
                    width: hoverLabel.width + 16; height: 26; radius: 6; color: "#E6000000"; border.color: "#44FFFFFF"
                    Text { id: hoverLabel; anchors.centerIn: parent; color: "white"; font.pixelSize: 13
                           text: ov.fmt(timeline.hoverTime) }
                }
            }
            MouseArea {
                id: tlMouse; anchors { fill: parent; topMargin: -8; bottomMargin: -6 } hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                function frac(x) { return Math.max(0, Math.min(1, x / width)) }
                onPositionChanged: (m) => { timeline.hoverFrac = frac(m.x); if (pressed) { timeline.dragFrac = frac(m.x); ov.interacted() }
                                            timeline.refreshThumb(); timeline.askThumb() }
                onPressed: (m) => { timeline.dragging = true; timeline.dragFrac = frac(m.x); ov.interacted() }
                onReleased: { if (timeline.duration > 0) { ov.player.seek(timeline.duration * timeline.dragFrac, false); ov.seeked() } timeline.dragging = false }
                onCanceled: timeline.dragging = false
            }
        }

        // Fila de botones
        Item {
            anchors { left: parent.left; right: parent.right; leftMargin: 20; rightMargin: 20; bottom: parent.bottom; bottomMargin: 14 }
            height: 44
            Row {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                spacing: 2
                IconBtn { icon: ov.player && ov.player.paused ? "play" : "pause"; tip: ov.player && ov.player.paused ? "Reproducir (Espacio)" : "Pausa (Espacio)"
                          onClicked: { ov.player.togglePause(); ov.flashCenter(ov.player.paused ? "play" : "pause", "") } }
                IconBtn { icon: "back10"; tip: "Retroceder 10 s (←)"; onClicked: { ov.player.seek(-10, true); ov.flashSkip(-10); ov.seeked() } }
                IconBtn { icon: "fwd10"; tip: "Adelantar 10 s (→)"; onClicked: { ov.player.seek(10, true); ov.flashSkip(10); ov.seeked() } }
                IconBtn { icon: "next"; visible: ov.isEpisode && ov.hasNext; tip: "Siguiente episodio (N)"; onClicked: ov.nextRequested() }
                IconBtn { icon: ov.player && (ov.player.muted || ov.player.volume === 0) ? "mute" : (ov.player && ov.player.volume < 60 ? "volume-low" : "volume")
                          tip: "Silenciar (M)"; onClicked: { ov.player.muted = !ov.player.muted; ov.flashVolume() } }
                FlatSlider { width: 110; anchors.verticalCenter: parent.verticalCenter; from: 0; to: 150; mark: 100
                             value: ov.player && ov.player.muted ? 0 : (ov.player ? ov.player.volume : 0)
                             onMoved: (v) => { if (ov.player.muted) ov.player.muted = false; ov.player.volume = Math.round(v); ov.flashVolume() } }
                Item { width: 8; height: 1 }
                Text { anchors.verticalCenter: parent.verticalCenter; width: 40; color: "#DDDDDD"; font.pixelSize: 13
                       text: Math.round(ov.player ? ov.player.volume : 0) + " %" }
                Item { width: 12; height: 1 }
                Text {
                    anchors.verticalCenter: parent.verticalCenter; color: "white"; font.pixelSize: 15
                    text: ov.fmt(ov.player ? ov.player.position : 0) + " / " + (ov.showRemaining ? "−" + ov.fmt(ov.remaining) : ov.fmt(ov.player ? ov.player.duration : 0))
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { ov.showRemaining = !ov.showRemaining; ov.interacted() } }
                }
            }
            Row {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 2
                IconBtn { id: audioBtn; icon: "audio"; tip: "Audio (A)"; onClicked: ov.openAudioMenu(audioBtn) }
                IconBtn { id: subBtn; icon: "subtitles"; tip: "Subtítulos (S)"; active: ov.player && ov.player.subtitleId !== "no" && ov.player.subtitleId !== "auto" && ov.player.subtitleId !== ""
                          onClicked: ov.openSubMenu(subBtn) }
                IconBtn { id: infoBtn; icon: "info"; tip: "Información técnica"; onClicked: ov.openInfo() }
                IconBtn { icon: ov.fullscreen ? "exit-fullscreen" : "fullscreen"; tip: "Pantalla completa (F)"; onClicked: ov.fullscreenRequested() }
                IconBtn { icon: "close"; tip: "Cerrar reproductor"; onClicked: ov.closeRequested() }
            }
        }
    }

    // ============================ Menús de pistas ============================
    ItemMenu { id: trackMenu; onClosed: ov.focusRequested() }
    function openMenuAbove(btn, entries, heading) {
        trackMenu.heading = heading
        trackMenu.entries = entries
        const p = btn.mapToItem(ov, 0, 0)
        const h = entries.reduce((a, e) => a + (e.header ? 30 : 38), 0) + 40
        trackMenu.showAt(p.x + btn.width - trackMenu.width, p.y - h - 8)
    }
    function openAudioMenu(btn) {
        const list = player.audioTracks
        const entries = list.map(t => ({ label: trackLabel(t), icon: t.id === player.audioId ? "check" : "blank",
                                         run: () => { player.setAudio(t.id); ov.seeked() } }))
        if (!entries.length) entries.push({ label: "Sin pistas de audio", icon: "blank" })
        openMenuAbove(btn, entries, "AUDIO")
    }
    function openSubMenu(btn) {
        const off = player.subtitleId === "no" || player.subtitleId === "" || player.subtitleId === "auto"
        const entries = [{ label: "Desactivados", icon: off ? "check" : "blank", run: () => { player.setSubtitle("no"); ov.seeked() } }]
        player.subtitleTracks.forEach(t => entries.push({ label: trackLabel(t), icon: t.id === player.subtitleId ? "check" : "blank",
                                                         run: () => { player.setSubtitle(t.id); ov.seeked() } }))
        entries.push({ header: "AJUSTES" })
        entries.push({ label: "Configuración de subtítulos…", icon: "subtitles", run: () => subSettings.open() })
        openMenuAbove(btn, entries, "SUBTÍTULOS")
    }
    readonly property alias audioButton: audioBtn
    readonly property alias subButton: subBtn
    function openSubSettings() { subSettings.open() }
    function toggleSubtitles() { openSubMenu(subBtn) }

    // ============================ Configuración de subtítulos ============================
    Modal {
        id: subSettings
        heading: "Configuración de subtítulos"
        dimmed: false; atTop: true; dialogWidth: 420
        onClosed: ov.focusRequested()
        Column {
            width: parent.width; spacing: 18
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13
                   visible: !(ov.player.subtitleId !== "no" && ov.player.subtitleId !== "" && ov.player.subtitleId !== "auto")
                   text: "Activa un subtítulo para ver la vista previa sobre el video." }
            Column { width: parent.width; spacing: 6
                Row { width: parent.width
                    Text { width: parent.width - 60; text: "Tamaño"; color: Tokens.text; font.pixelSize: 14 }
                    Text { text: Math.round(ov.subSize); color: Tokens.textMuted; font.pixelSize: 14 } }
                FlatSlider { width: parent.width; from: 20; to: 100; value: ov.subSize
                             onMoved: (v) => { ov.subSize = Math.round(v); ov.player.setSubtitleStyle(ov.subSize, ov.subPos); ov.saveSubStyle() } }
            }
            Column { width: parent.width; spacing: 6
                Row { width: parent.width
                    Text { width: parent.width - 60; text: "Posición vertical"; color: Tokens.text; font.pixelSize: 14 }
                    Text { text: Math.round(ov.subPos) + " %"; color: Tokens.textMuted; font.pixelSize: 14 } }
                FlatSlider { width: parent.width; from: 40; to: 100; value: ov.subPos
                             onMoved: (v) => { ov.subPos = Math.round(v); ov.player.setSubtitleStyle(ov.subSize, ov.subPos); ov.saveSubStyle() } }
                Text { text: "100 % = parte baja de la pantalla"; color: Tokens.textFaint; font.pixelSize: 12 }
            }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Restablecer"; onClicked: { ov.subSize = 55; ov.subPos = 100; ov.player.setSubtitleStyle(55, 100); ov.saveSubStyle() } }
                AdminButton { text: "Listo"; primary: true; onClicked: subSettings.close() }
            }
        }
    }

    // ============================ Información técnica ============================
    property var tech: ({})
    function openInfo() { tech = player.techInfo(); infoPanel.open() }
    Popup {
        id: infoPanel
        parent: Overlay.overlay
        x: ov.width - width - 24; y: ov.height - 190 - height
        width: 380; padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: ov.focusRequested()
        background: Rectangle { radius: Tokens.radius; color: "#F2191821"; border.color: Tokens.borderStrong }
        contentItem: Column {
            spacing: 6
            Text { text: "INFORMACIÓN TÉCNICA"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.4 }
            Repeater {
                model: [
                    ["Reproducción", "Archivo original · sin conversión"],
                    ["Contenedor", ov.tech.contenedor], ["Resolución", ov.tech.resolucion],
                    ["Video", (ov.tech.videoCodec || "").split(" ")[0] + (ov.tech.fps ? " · " + Math.round(ov.tech.fps * 100) / 100 + " fps" : "")],
                    ["Decodificación", ov.tech.hwdec && ov.tech.hwdec !== "no" ? "Hardware (" + ov.tech.hwdec + ")" : "Software"],
                    ["Audio", (ov.tech.audioCodec || "") + (ov.tech.audioCanales ? " · " + ov.tech.audioCanales : "")],
                    ["Fotogramas perdidos", ov.tech.perdidos]
                ]
                delegate: Row {
                    required property var modelData
                    visible: !!modelData[1]
                    spacing: 10
                    Text { width: 130; text: modelData[0]; color: Tokens.textMuted; font.pixelSize: 13 }
                    Text { width: 190; text: modelData[1] || ""; color: Tokens.text; font.pixelSize: 13; elide: Text.ElideRight }
                }
            }
            Text { width: parent.width; text: ov.tech.archivo || ""; color: Tokens.textFaint; font.pixelSize: 11; elide: Text.ElideMiddle }
        }
    }

    // ============================ OSD ============================
    Rectangle { // volumen (arriba a la derecha)
        anchors { right: parent.right; rightMargin: 28; top: parent.top; topMargin: 56 }
        width: 250; height: 56; radius: 14; color: "#D9000000"
        opacity: volTimer.running ? 1 : 0; visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        NavIcon { id: volIcon; anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter } width: 28; height: 28
                  name: ov.player && (ov.player.muted || ov.player.volume === 0) ? "mute" : "volume"; color: "white" }
        Rectangle { id: volRail; anchors { left: volIcon.right; leftMargin: 12; verticalCenter: parent.verticalCenter } width: 110; height: 5; radius: 3; color: "#44FFFFFF"
            Rectangle { height: parent.height; radius: 3; color: ov.player && ov.player.volume > 100 ? Tokens.warning : "white"
                        width: ov.player ? parent.width * (ov.player.muted ? 0 : ov.player.volume / 150) : 0 } }
        Text { anchors { left: volRail.right; leftMargin: 12; verticalCenter: parent.verticalCenter } color: "white"; font.pixelSize: 22; font.weight: Font.DemiBold
               text: ov.player ? (ov.player.muted ? "Silencio" : Math.round(ov.player.volume) + " %") : "" }
    }
    Rectangle { // centro: pausa / reproducir / saltos
        anchors.centerIn: parent
        width: osdRow.width + 56; height: 88; radius: 20; color: "#CC000000"
        opacity: osdTimer.running ? 1 : 0; visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Row { id: osdRow; anchors.centerIn: parent; spacing: 14
            NavIcon { width: 40; height: 40; anchors.verticalCenter: parent.verticalCenter; name: ov.osdIcon; color: "white"; lineWidth: 2.2 }
            Text { visible: ov.osdText !== ""; anchors.verticalCenter: parent.verticalCenter; text: ov.osdText; color: "white"; font.pixelSize: 30; font.weight: Font.DemiBold }
        }
    }

    // Carga / buffer
    Item {
        anchors.centerIn: parent; width: 120; height: 120
        visible: ov.player && (ov.player.buffering || ov.player.state === "cargando") && !ov.failed && !ov.ended
        Canvas {
            id: spinner; anchors.centerIn: parent; width: 64; height: 64
            onPaint: { const c = getContext("2d"); c.reset(); c.lineWidth = 5; c.lineCap = "round"
                       c.strokeStyle = "#33FFFFFF"; c.beginPath(); c.arc(32, 32, 26, 0, Math.PI * 2); c.stroke()
                       c.strokeStyle = "white"; c.beginPath(); c.arc(32, 32, 26, 0, Math.PI * 0.55); c.stroke() }
            RotationAnimator on rotation { from: 0; to: 360; duration: 900; loops: Animation.Infinite; running: spinner.visible }
        }
        Text { anchors { horizontalCenter: parent.horizontalCenter; top: spinner.bottom; topMargin: 8 } color: "#DDDDDD"; font.pixelSize: 13
               text: ov.player && ov.player.buffering ? "Cargando buffer…" : "" }
    }

    // Aviso de «Esc de nuevo»
    Rectangle {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 64 }
        width: exitText.width + 40; height: 44; radius: 22; color: "#E6000000"; border.color: "#44FFFFFF"
        opacity: ov.exitArmed ? 1 : 0; visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Text { id: exitText; anchors.centerIn: parent; text: "Presiona Esc de nuevo para salir"; color: "white"; font.pixelSize: 15 }
    }

    // Tarjeta «Siguiente episodio»
    Rectangle {
        id: nextCard
        anchors { right: parent.right; rightMargin: 28; bottom: parent.bottom; bottomMargin: ov.controlsVisible ? 184 : 40 }
        width: 330; height: 132; radius: Tokens.radius + 2; color: "#F2191821"; border.color: Tokens.borderStrong
        opacity: ov.nextCardShown ? 1 : 0; visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on anchors.bottomMargin { NumberAnimation { duration: 180 } }
        MouseArea { anchors.fill: parent }
        Column {
            anchors { fill: parent; margins: 16 } spacing: 4
            Text { text: "Siguiente episodio en " + ov.countdown + " s"; color: Tokens.accentSoft; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
            Text { width: parent.width; text: ov.nextTitle; color: "white"; font.pixelSize: 16; font.weight: Font.DemiBold; elide: Text.ElideRight }
            Item { width: 1; height: 8 }
            Row { spacing: 10
                AdminButton { text: "▶  Reproducir ya"; primary: true; implicitHeight: 38; onClicked: ov.nextRequested() }
                AdminButton { text: "Cancelar"; implicitHeight: 38; onClicked: ov.nextCancelled = true }
            }
        }
    }

    // ============================ Pantalla final ============================
    Rectangle {
        anchors.fill: parent; color: Tokens.bg
        opacity: ov.ended ? 1 : 0; visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 320 } }
        MouseArea { anchors.fill: parent; hoverEnabled: true }
        Image { anchors.fill: parent; source: ov.backdropUrl ? Images.src(ov.backdropUrl) : ""; fillMode: Image.PreserveAspectCrop
                verticalAlignment: Image.AlignTop; asynchronous: true; sourceSize.width: 1600; opacity: 0.55 }
        Rectangle { anchors.fill: parent; gradient: Gradient {
            GradientStop { position: 0.0; color: "#990E0D11" }
            GradientStop { position: 0.6; color: "#CC0E0D11" }
            GradientStop { position: 1.0; color: Tokens.bg } } }
        Column {
            anchors.centerIn: parent; spacing: 10; width: Math.min(parent.width - 96, 640)
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "FIN DE LA REPRODUCCIÓN"; color: Tokens.accentSoft; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.6 }
            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: ov.title
                   color: Tokens.text; font.pixelSize: 36; font.family: Tokens.fontDisplay; font.weight: Font.DemiBold }
            Item { width: 1; height: 14 }
            Row { anchors.horizontalCenter: parent.horizontalCenter; spacing: 12
                AdminButton { text: "‹  Volver a la ficha"; primary: true; implicitHeight: 48; onClicked: ov.closeRequested() }
                AdminButton { visible: ov.isEpisode && ov.hasNext; text: "Siguiente episodio  ›"; implicitHeight: 48; onClicked: ov.nextRequested() }
                AdminButton { text: "↺  Volver a ver"; implicitHeight: 48; onClicked: ov.replayRequested() }
            }
        }
    }

    // ============================ Pantalla de error ============================
    Rectangle {
        anchors.fill: parent; color: "#F20E0D11"
        opacity: ov.failed ? 1 : 0; visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        MouseArea { anchors.fill: parent; hoverEnabled: true }
        Column {
            anchors.centerIn: parent; spacing: 14; width: Math.min(parent.width - 96, 620)
            Text { text: "No se pudo reproducir"; color: Tokens.text; font.pixelSize: 26; font.weight: Font.DemiBold }
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.35
                   text: "mpv no pudo abrir o decodificar «" + ov.title + "». Comprueba que el archivo existe y que el disco o la red están disponibles." }
            Rectangle { width: parent.width; height: 150; radius: Tokens.radiusSmall; color: Tokens.surface; border.color: Tokens.border; clip: true
                Flickable { anchors { fill: parent; margins: 10 } contentHeight: detail.height; clip: true; boundsBehavior: Flickable.StopAtBounds
                    TextEdit { id: detail; width: parent.width; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                               color: Tokens.textMuted; font.pixelSize: 12; font.family: "Consolas"; text: ov.player ? ov.player.errorText : "" } } }
            Row { spacing: 10
                AdminButton { text: "Reintentar"; primary: true; onClicked: ov.retryRequested() }
                AdminButton { text: "Copiar detalle"; onClicked: { detail.selectAll(); detail.copy(); detail.deselect() } }
                AdminButton { text: "Volver"; onClicked: ov.closeRequested() }
            }
        }
    }
}

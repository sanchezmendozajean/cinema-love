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
    onClosing: { persist(); player.shutdown() }

    property bool controlsVisible: true
    property bool ended: false            // fin de reproducción: se muestra la pantalla final
    property bool cursorHidden: false
    readonly property string nextEpisodeId: nowPlaying && nowPlaying.kind === "episode" ? seriesCat.nextEpisodeId(nowPlaying.id) : ""
    readonly property string nextEpisodeLabel: {
        if (nextEpisodeId === "") return ""
        const e = seriesCat.episode(nextEpisodeId)
        return (e.label ? e.label + " — " : "") + (e.title || "")
    }
    function toggleFullscreen() { visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    function playNext() {
        const id = nextEpisodeId
        if (!id || !nowPlaying) return
        const cur = nowPlaying
        markWatched()
        refreshDetailAfter("episode", cur.id)
        playEpisodeById(id, 0)
    }
    function retry() { if (nowPlaying) { ended = false; player.load(nowPlaying.path, player.position) } }
    function replay() { if (nowPlaying) { ended = false; watchedMarked = true; player.load(nowPlaying.path, 0) } }
    property bool playing: false          // true cuando se está viendo una película (si no, se muestra el catálogo)
    property string movieTitle: ""
    property string notice: ""
    // Sección principal: «home» (Inicio), «catalog» (Películas), «series», «collections» o «admin» (Administración). Las pruebas del catálogo arrancan en «catalog».
    property string section: spikeArgs.section || ((spikeArgs.catalogShot || spikeArgs.progressTest || spikeArgs.openFirst) ? "catalog" : "home")
    // Barra lateral: visible salvo durante la reproducción y el asistente; compacta (solo iconos) en ventanas estrechas.
    readonly property bool navVisible: !playing && !(wizardActive || !dataReady)
    readonly property bool navCompact: width < 1100
    property int navWidth: navVisible ? (navCompact ? 72 : 232) : 0
    Behavior on navWidth { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    property var person: null   // ficha de persona abierta (encima de la ficha de la que se llegó) o null
    property var detail: null   // ficha abierta (película de MovieModel o serie de SeriesCatalog, con kind === "series") o null
    readonly property bool seriesOpen: detail !== null && detail.kind === "series"
    readonly property bool movieOpen: detail !== null && detail.kind !== "series"

    // Los modelos leen la base solo cuando la colección está abierta: el backend (respaldo, bloqueo, migraciones) va primero.
    readonly property bool wizardMode: wizardActive
    property bool wizardActive: dataFolder.path === "" && !spikeArgs.clientAuto
    readonly property bool dataReady: clientMode ? clientReady : (dataFolder.path !== "" && (spikeArgs.noBackend || !spikeArgs.dataDir || backend.state === "listo"))
    readonly property string dbFile: clientMode ? mirrorDb : (dataReady ? dataFolder.dbFile : "")

    // --- Modo Cliente: el catálogo de otro CinemaLove se refleja en un espejo local (backend/src/main/client/espejo.ts) que leen los mismos modelos ---
    readonly property bool clientMode: backend.clientMode
    property var clientConn: ({ tipo: "desconectada" })
    property var clientMirror: ({ fase: "inactivo", hecho: 0, total: 0, ruta: null })
    property var clientRemembered: []
    property string mirrorDb: ""
    property string lastMirrorPhase: ""
    property bool clientAutoDone: false
    readonly property bool clientReady: clientMode && clientConn.tipo === "conectada" && mirrorDb !== ""
    readonly property string serverName: clientConn.info ? clientConn.info.nombre : ""
    Binding { target: Tokens; property: "clientMode"; value: root.clientMode }
    ApiClient { id: ctl; baseUrl: backend.controlUrl; token: backend.controlToken }   // servidor de control del sidecar (solo Modo Cliente)
    function enterClientMode() {
        wizardActive = false; mirrorDb = ""; clientConn = ({ tipo: "desconectada" }); clientAutoDone = true; lastMirrorPhase = ""
        clientView.reset()
        backend.restartClient(spikeArgs.clientDir)
    }
    function leaveClientMode() {
        prefs.remove("modo"); prefs.remove("servidorId")
        mirrorDb = ""; clientConn = ({ tipo: "desconectada" })
        if (dataFolder.path !== "") backend.restart(dataFolder.path)
        else { backend.stop(); wizardActive = true; startup.step = 0 }
        movies.reload(); seriesCat.reload()
    }
    Timer {
        interval: 800; repeat: true; running: root.clientMode && ctl.ready
        onTriggered: ctl.request("GET", "/control/estado", null, (st, d) => {
            if (st !== 200 || !d) { console.log("CLIENT estado fallo:", st); return }
            root.clientConn = d.conexion; root.clientMirror = d.espejo; root.clientRemembered = d.recordados
            if (d.conexion.tipo === "conectada") { if (backend.baseUrl !== d.conexion.api.baseUrl) backend.setApi(d.conexion.api.baseUrl, d.conexion.api.token) }
            else if (backend.baseUrl !== backend.controlUrl) backend.setApi("", "")
            if (d.espejo.ruta && root.mirrorDb !== d.espejo.ruta) root.mirrorDb = d.espejo.ruta
            if (d.espejo.fase === "listo" && root.lastMirrorPhase !== "listo") { movies.reload(); seriesCat.reload() }
            root.lastMirrorPhase = d.espejo.fase
        })
    }
    Connections {   // al estar listo el sidecar en Modo Cliente: reconectar al servidor recordado (o la conexión de prueba)
        target: backend
        function onReadyChanged() {
            if (!backend.clientMode || backend.controlUrl === "" || root.clientAutoDone) return
            root.clientAutoDone = true
            if (spikeArgs.clientConnect) {
                ctl.request("POST", "/control/conectar", { url: spikeArgs.clientConnect, credencial: { tipo: "apiKey", apiKey: spikeArgs.clientKey }, recordar: true }, (st, d, err) => { if (st !== 200 || !d || !d.ok) console.log("CLIENT conectar falló:", st, JSON.stringify(d), err || "") })
            } else {
                const id = prefs.value("servidorId", "")
                if (id) ctl.request("POST", "/control/reconectar", { id: id }, () => {})
            }
        }
    }
    // Lo que el usuario cambia se manda al servidor; el espejo local solo se refresca.
    function pushProgress(id, pos, state, audio, sub) {
        if (!clientMode) return
        const body = { estado: state }
        if (state === "enProgreso") { body.posicionSegundos = Math.round(pos); if (audio) body.pistaAudioId = audio; if (sub) body.pistaSubtituloId = sub }
        api.request("PUT", "/api/v1/media/" + id + "/progress", body, () => {})
    }
    MovieModel { id: movies; dbPath: root.dbFile }
    SeriesCatalog { id: seriesCat; dbPath: root.dbFile }

    // Backend (sidecar Node): lo crea y lo inicia main.cpp (`backend` es una propiedad de contexto); aquí solo se consume.
    ApiClient { id: api; baseUrl: backend.baseUrl; token: backend.token }
    readonly property string backendNote: spikeArgs.dataDir ? ""
        : "La administración necesita una carpeta de datos con un archivo cinemalove.sqlite."

    // --- Progreso (specs/05 §4): se guarda solo al pausar, hacer seek, cambiar de pista, cerrar y al superar el 95 % ---
    property var nowPlaying: null       // ficha en reproducción (null en el modo --selftest / archivo suelto)
    property bool progressReady: false  // no se guarda hasta restaurar las pistas elegidas (evita pisarlas con «auto»)
    property bool watchedMarked: false
    property var pendingTracks: null

    readonly property var store: nowPlaying && nowPlaying.kind === "episode" ? seriesCat : movies   // dónde se guarda el progreso

    function persist() {
        if (!nowPlaying || watchedMarked || !progressReady || player.position < 5) return
        store.saveProgress(nowPlaying.id, player.position, "enProgreso", String(player.audioId), String(player.subtitleId))
        pushProgress(nowPlaying.id, player.position, "enProgreso", String(player.audioId), String(player.subtitleId))
    }
    function markWatched() {
        if (!nowPlaying || watchedMarked) return
        watchedMarked = true
        store.saveProgress(nowPlaying.id, 0, "visto")
        pushProgress(nowPlaying.id, 0, "visto")
    }

    Timer { id: saveTimer; interval: 700; onTriggered: root.persist() }   // agrupa pausa/seek/pista en un solo guardado
    Timer { id: readyTimer; interval: 1500; onTriggered: root.progressReady = true }
    Timer { id: readyFallback; interval: 6000; onTriggered: root.progressReady = true }

    Connections {
        target: player
        function onPausedChanged() { if (root.playing) saveTimer.restart() }
        function onPositionChanged() {
            if (root.nowPlaying && !root.watchedMarked && player.duration > 0 && player.position / player.duration > 0.95) root.markWatched()
        }
        function onTracksChanged() {   // reselección única de audio/subtítulo guardados, cuando ya se conoce la lista real
            if (!root.pendingTracks || player.audioTracks.length === 0) return
            const t = root.pendingTracks
            root.pendingTracks = null
            if (t.a && t.a !== "auto") player.setAudio(t.a)
            if (t.s && t.s !== "auto") player.setSubtitle(t.s)
            readyTimer.restart()
        }
    }

    function showDetail(row) { openDetailOf(movies.get(row)) }
    function showDetailById(id) { openDetailOf(movies.getById(id)) }
    function playById(id) {
        const m = movies.getById(id)
        m.available = movies.fileExists(m.path)
        detail = m
        openMovie(m, m.progressState === "enProgreso" ? m.progressPos : 0)
    }
    function openDetailOf(m) {
        m.available = movies.fileExists(m.path)
        detail = m
        detailView.forceActiveFocus()
    }
    function closeDetail() { if (person) { closePerson(); return } detail = null; catalog.forceActiveFocus() }
    function openPerson(p) { if (p && p.id) { person = p; personView.forceActiveFocus() } }
    function closePerson() { person = null; if (detail) (seriesOpen ? seriesDetailView : detailView).forceActiveFocus() }
    function openFromPerson(it) {   // desde «En tu colección»: se abre la ficha del título (la persona queda atrás)
        person = null
        if (it.kind === "pelicula") showDetailById(it.id); else showSeriesById(it.serieId || it.seriesId || it.id)
    }

    // --- Series ---
    function openSeries(d) {
        if (!d || !d.id) return
        detail = d
        seriesDetailView.forceActiveFocus()
    }
    function showSeriesAt(row) { openSeries(seriesCat.detailAt(row)) }
    function showSeriesById(id) { openSeries(seriesCat.detail(id)) }
    function playEpisodeById(id, start) {
        const ep = seriesCat.episode(id)
        if (!ep.id) return
        if (!(detail && detail.id === ep.seriesId)) detail = seriesCat.detail(ep.seriesId)
        openMovie(ep, start === undefined ? (ep.progressState === "enProgreso" ? ep.progressPos : 0) : start)
    }
    // --- Menú ⋯ y acciones del usuario (Próximas a ver, vista/no vista, colecciones) vía API del backend ---
    function refreshAfterMutation() {
        if (clientMode) { ctl.request("POST", "/control/personal", null, () => { movies.reload(); seriesCat.reload(); refreshAfterMutationViews() }); return }
        movies.reload(); seriesCat.reload()
        refreshAfterMutationViews()
    }
    function refreshAfterMutationViews() {
        if (collectionsView.visible) collectionsView.reload()
        if (seriesOpen) detail = seriesCat.detail(detail.id)
        else if (movieOpen) { const m = movies.getById(detail.id); m.available = true; detail = m }
    }
    function mutate(method, path, body, okMsg) {
        api.request(method, path, body === undefined ? null : body, (st, d, err) => {
            if (st >= 200 && st < 300) { if (okMsg) toast(okMsg); refreshAfterMutation() } else toast(err || "No se pudo completar la acción.")
        })
    }
    function toast(t) { notice = t; noticeTimer.restart() }

    function openItemMenu(it, x, y) {
        if (!api.ready) { toast("El servicio aún está iniciando; inténtalo en unos segundos."); return }
        if (it.kind === "series" || it.kind === "serie") {
            const entries = [
                { label: "Marcar serie como vista", icon: "check", run: () => confirmMark(it.id, null, "visto", it.title) },
                { label: "Marcar serie como no vista", icon: "uncheck", run: () => confirmMark(it.id, null, "noVisto", it.title) }
            ]
            itemMenu.heading = it.title; itemMenu.entries = entries; itemMenu.showAt(x, y)
            return
        }
        const id = it.id
        api.request("GET", "/api/v1/media/" + id + "/user-state", null, (st, us) => {
            if (st !== 200) { toast("No se pudo leer el estado del título."); return }
            api.request("GET", "/api/v1/collections", null, (st2, cols) => {
                const list = st2 === 200 ? cols : []
                const seen = us.progreso && us.progreso.estado === "visto"
                const inCols = (us.colecciones || []).map(c => c.id)
                const entries = [
                    { label: us.enProximas ? "Quitar de Próximas a ver" : "Agregar a Próximas a ver", icon: us.enProximas ? "bookmark-filled" : "bookmark",
                      run: () => mutate(us.enProximas ? "DELETE" : "PUT", "/api/v1/watchlist/" + id, null, us.enProximas ? "Quitada de Próximas a ver." : "Agregada a Próximas a ver.") },
                    { label: seen ? "Marcar como no vista" : "Marcar como vista", icon: seen ? "uncheck" : "check",
                      run: () => mutate("PUT", "/api/v1/media/" + id + "/progress", { estado: seen ? "noVisto" : "visto" }, seen ? "Marcada como no vista." : "Marcada como vista.") }
                ]
                if (list.length) entries.push({ header: "COLECCIONES" })
                list.forEach(c => {
                    const inC = inCols.indexOf(c.id) >= 0
                    entries.push({ label: (inC ? "Quitar de «" : "Agregar a «") + c.nombre + "»", icon: inC ? "collection-remove" : "collection-add",
                        run: () => mutate(inC ? "DELETE" : "POST", inC ? "/api/v1/collections/" + c.id + "/items/" + id : "/api/v1/collections/" + c.id + "/items",
                                          inC ? null : { mediaItemId: id }, inC ? "Quitada de «" + c.nombre + "»." : "Agregada a «" + c.nombre + "».") })
                })
                itemMenu.heading = it.title; itemMenu.entries = entries; itemMenu.showAt(x, y)
            })
        })
    }

    // «No vista» borra el progreso (posiciones de reanudación) y marcar una serie toca muchos episodios: siempre se confirma.
    property var markTarget: ({ seriesId: "", season: null, estado: "visto", title: "" })
    function confirmMark(seriesId, season, estado, title) { markTarget = { seriesId: seriesId, season: season, estado: estado, title: title }; markConfirm.open() }

    function openCollectionItem(it) {
        if (it.kind === "pelicula") showDetailById(it.id)
        else showSeriesById(it.seriesId || it.id)
    }

    function openItemDetail(it) { if (it.kind === "episode") showSeriesById(it.seriesId); else showDetailById(it.id) }
    function playItem(it) {
        if (it.kind === "episode") playEpisodeById(it.id)
        else playById(it.id)
    }
    function currentCatalog() { return section === "series" ? seriesCatalog : catalog }

    function openMovie(m, start) {
        if (!movies.fileExists(m.path)) {
            notice = "No se encuentra el archivo de «" + m.title + "». ¿Está conectado el disco " + m.path.substring(0, 2) + "?"
            noticeTimer.restart()
            return
        }
        nowPlaying = m.id ? m : null
        ended = false
        overlay.reset()
        watchedMarked = false
        progressReady = false
        pendingTracks = m.id ? { a: m.progressAudio, s: m.progressSub } : null
        if (m.id) readyFallback.restart()
        movieTitle = m.kind === "episode" ? m.seriesTitle + "  ·  " + m.label + " — " + m.title
                                          : m.title + (m.year > 0 ? " (" + m.year + ")" : "")
        playing = true
        controlsVisible = true
        player.load(m.path, start || 0)
        hideTimer.restart()
        player.forceActiveFocus()
    }
    // La ficha abierta refleja el progreso recién guardado.
    function refreshDetailAfter(kind, id) {
        if (!detail) return
        if (kind === "episode" && seriesOpen) detail = seriesCat.detail(detail.id)
        else if (kind !== "episode" && movieOpen && detail.id === id) {
            const m = movies.getById(id)
            m.available = true
            detail = m
        }
    }
    function closeMovie() {
        persist()
        saveTimer.stop()
        const kind = nowPlaying ? nowPlaying.kind : ""
        const id = nowPlaying ? nowPlaying.id : ""
        nowPlaying = null
        ended = false
        pendingTracks = null
        player.stop()
        if (id) refreshDetailAfter(kind, id)
        if (visibility === Window.FullScreen) visibility = Window.Windowed
        playing = false
        movieTitle = ""
        hideTimer.stop()
        if (detail) (seriesOpen ? seriesDetailView : detailView).forceActiveFocus()
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
        keepAwake: root.playing && !paused && !root.ended && state !== "error"   // sin apagado de pantalla mientras se ve
        anchors.fill: parent
        focus: true
        onEndOfFile: (reason) => {
            if (reason !== "eof" || spikeArgs.selftest || !root.nowPlaying) return
            root.markWatched()
            root.refreshDetailAfter(root.nowPlaying.kind, root.nowPlaying.id)
            if (root.nextEpisodeId !== "" && !overlay.nextCancelled) root.playNext()   // salto automático al siguiente episodio
            else root.ended = true                                                      // pantalla final
        }

        Keys.onPressed: (e) => {
            const k = e.key, sec = (e.modifiers & Qt.ShiftModifier) ? 30 : 10
            if (root.ended || overlay.failed) {
                if (k === Qt.Key_Escape || k === Qt.Key_Backspace) root.closeMovie()
                else return
                e.accepted = true
                return
            }
            switch (k) {
            case Qt.Key_Space: togglePause(); overlay.flashCenter(paused ? "play" : "pause", ""); break
            case Qt.Key_Right: seek(sec, true); overlay.flashSkip(sec); saveTimer.restart(); break
            case Qt.Key_Left: seek(-sec, true); overlay.flashSkip(-sec); saveTimer.restart(); break
            case Qt.Key_Up: overlay.adjustVolume(5); break          // el volumen no muestra la barra de controles
            case Qt.Key_Down: overlay.adjustVolume(-5); break
            case Qt.Key_M: muted = !muted; overlay.flashVolume(); break
            case Qt.Key_F: root.toggleFullscreen(); break
            case Qt.Key_N: if (root.nextEpisodeId !== "") root.playNext(); break
            case Qt.Key_Escape:
                if (overlay.exitArmed) root.closeMovie()
                else overlay.armExit()   // Esc no sale de pantalla completa (F o el botón); la segunda pulsación cierra el reproductor
                break
            case Qt.Key_A: { const n = root.cycle(audioTracks, audioId, false); if (n) setAudio(n); saveTimer.restart(); break }
            case Qt.Key_S: { const n = root.cycle(subtitleTracks, subtitleId, true); if (n) setSubtitle(n); saveTimer.restart(); break }
            default: return
            }
            // Solo las teclas de reproducción reinician el ocultado; ninguna muestra los controles.
            hideTimer.restart()
            e.accepted = true
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.cursorHidden && !root.controlsVisible ? Qt.BlankCursor : Qt.ArrowCursor
            onPositionChanged: { root.cursorHidden = false; cursorTimer.restart() }
            onClicked: { root.controlsVisible = !root.controlsVisible; hideTimer.restart() }
            onDoubleClicked: root.toggleFullscreen()
            onWheel: (w) => overlay.adjustVolume(w.angleDelta.y > 0 ? 5 : -5)
        }
        DragHandler {   // en modo ventana se arrastra desde cualquier punto del video
            target: null
            enabled: !root.fullscreen && root.visibility !== Window.Maximized
            onActiveChanged: if (active) root.startSystemMove()
        }
    }

    CatalogView {
        id: catalog
        anchors { fill: parent; leftMargin: root.navWidth }
        readonly property bool pageActive: !root.playing && !root.detail && root.section === "catalog"
        opacity: pageActive ? 1 : 0                      // fundido suave entre secciones
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        model: movies
        total: movies.total
        matches: movies.matchCount
        page: movies.page
        pageCount: movies.pageCount
        onPageRequested: (p) => movies.page = p
        error: movies.error
        onSearch: (t) => movies.filter = t
        onMenuRequested: (it, x, y) => root.openItemMenu(it, x, y)
        onMovieChosen: (row) => root.showDetail(row)
    }

    CatalogView {
        id: seriesCatalog
        anchors { fill: parent; leftMargin: root.navWidth }
        readonly property bool pageActive: !root.playing && !root.detail && root.section === "series"
        opacity: pageActive ? 1 : 0                      // fundido suave entre secciones
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        model: seriesCat
        total: seriesCat.total
        matches: seriesCat.matchCount
        page: seriesCat.page
        pageCount: seriesCat.pageCount
        onPageRequested: (p) => seriesCat.page = p
        error: seriesCat.error
        heading: "Series"
        placeholder: "Buscar serie…"
        itemKind: "series"
        onSearch: (t) => seriesCat.filter = t
        onMenuRequested: (it, x, y) => root.openItemMenu(it, x, y)
        onMovieChosen: (row) => root.showSeriesAt(row)
    }

    AdminView {
        id: adminView
        anchors { fill: parent; leftMargin: root.navWidth }
        readonly property bool pageActive: !root.playing && !root.detail && root.section === "admin"
        opacity: pageActive ? 1 : 0                      // fundido suave entre secciones
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        api: api
        backendState: root.backendNote !== "" ? "error" : backend.state
        backendError: root.backendNote !== "" ? root.backendNote : backend.error
        onToast: (t) => { root.notice = t; noticeTimer.restart() }
        onCatalogChanged: { movies.reload(); seriesCat.reload() }
        onChangeFolder: { dataFolder.forget(); backend.stop(); root.wizardActive = true; startup.step = 1 }
    }

    CollectionsView {
        id: collectionsView
        anchors { fill: parent; leftMargin: root.navWidth }
        readonly property bool pageActive: !root.playing && !root.detail && root.section === "collections"
        opacity: pageActive ? 1 : 0                      // fundido suave entre secciones
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        api: api
        backendReady: backend.state === "listo"
        onOpenItem: (it) => root.openCollectionItem(it)
        onMenuRequested: (it, x, y) => root.openItemMenu(it, x, y)
        onToast: (t) => root.toast(t)
    }

    ItemMenu { id: itemMenu }

    AboutDialog {
        id: aboutDialog
        mpvVersion: (player.stats()["mpv-version"] || "").replace(/^mpv /, "")
        ffmpegVersion: player.stats()["ffmpeg-version"] || ""
    }

    Modal {
        id: markConfirm
        heading: root.markTarget.estado === "visto" ? "Marcar como vista" : "Marcar como no vista"
        Column {
            width: parent.width; spacing: 16
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14; lineHeight: 1.35
                   text: (root.markTarget.estado === "visto"
                          ? "Se marcarán como vistos "
                          : "Se quitará el progreso (incluidas las posiciones para reanudar) de ")
                         + (root.markTarget.season !== null ? "todos los episodios de la temporada " + root.markTarget.season + " de «" : "todos los episodios de «")
                         + root.markTarget.title + "»." + (root.markTarget.estado === "noVisto" ? " Tus archivos no se tocan." : "") }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: markConfirm.close() }
                AdminButton { text: root.markTarget.estado === "visto" ? "Marcar como vista" : "Marcar como no vista"; primary: true; danger: root.markTarget.estado === "noVisto"
                    onClicked: {
                        markConfirm.close()
                        root.mutate("PUT", "/api/v1/series/" + root.markTarget.seriesId + "/progress",
                                    root.markTarget.season !== null ? { estado: root.markTarget.estado, temporada: root.markTarget.season } : { estado: root.markTarget.estado },
                                    "Listo.")
                    } }
            }
        }
    }

    StartupView {
        id: startup
        z: 100                      // por encima de todas las vistas, por debajo de la barra de título
        anchors.fill: parent
        api: api
        wizard: root.wizardActive
        readonly property bool pageActive: (root.wizardActive || !root.dataReady) && !root.clientMode
        opacity: pageActive ? 1 : 0
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        onConnectRequested: root.enterClientMode()
        onFinished: { root.wizardActive = false; movies.reload(); seriesCat.reload() }
        onChooseAnotherFolder: { dataFolder.forget(); backend.stop(); root.wizardActive = true; startup.step = 1 }
    }

    ClientView {
        id: clientView
        z: 100
        anchors.fill: parent
        ctl: ctl
        conn: root.clientConn
        mirror: root.clientMirror
        remembered: root.clientRemembered
        readonly property bool pageActive: root.clientMode && !root.clientReady
        opacity: pageActive ? 1 : 0
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        onCancel: root.leaveClientMode()
        onConnectedOk: (id) => { if (id) { prefs.setValue("modo", "cliente"); prefs.setValue("servidorId", id) } else prefs.remove("modo") }
    }

    HomeView {
        id: homeView
        anchors { fill: parent; leftMargin: root.navWidth }
        readonly property bool pageActive: !root.playing && !root.detail && root.section === "home"
        opacity: pageActive ? 1 : 0                      // fundido suave entre secciones
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        model: movies
        series: seriesCat
        onOpenDetail: (it) => root.openItemDetail(it)
        onMenuRequested: (it, x, y) => root.openItemMenu(it, x, y)
        onPlayMovie: (it) => root.playItem(it)
    }

    SeriesDetailView {
        id: seriesDetailView
        anchors { fill: parent; leftMargin: root.navWidth }
        readonly property bool pageActive: !root.playing && root.seriesOpen && !root.person
        opacity: pageActive ? 1 : 0                      // fundido suave entre secciones
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        series: root.seriesOpen ? root.detail : ({})
        catalog: seriesCat
        onBack: root.closeDetail()
        onPersonRequested: (p) => root.openPerson(p)
        onPlayEpisode: (id, start) => root.playEpisodeById(id, start)
        onMarkRequested: (sid, season, estado) => root.confirmMark(sid, season, estado, root.detail.title)
    }

    DetailView {
        id: detailView
        anchors { fill: parent; leftMargin: root.navWidth }
        readonly property bool pageActive: !root.playing && root.movieOpen && !root.person
        opacity: pageActive ? 1 : 0                      // fundido suave entre secciones
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        movie: root.movieOpen ? root.detail : ({})
        onBack: root.closeDetail()
        onPersonRequested: (p) => root.openPerson(p)
        onPlay: (start) => root.openMovie(root.detail, start)
    }

    PersonView {
        id: personView
        anchors { fill: parent; leftMargin: root.navWidth }
        readonly property bool pageActive: !root.playing && root.person !== null
        opacity: pageActive ? 1 : 0
        visible: opacity > 0
        enabled: pageActive
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        api: api
        person: root.person
        onBack: root.closePerson()
        onOpenItem: (it) => root.openFromPerson(it)
        onMenuRequested: (it, x, y) => root.openItemMenu(it, x, y)
    }

    Rectangle { // aviso temporal (archivo no disponible)
        visible: root.notice !== "" && !root.playing
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 32 }
        width: Math.min(parent.width - 64, noticeText.implicitWidth + 40); height: 44; radius: Tokens.radius
        color: Tokens.surfaceRaised; border.color: Tokens.borderStrong
        Text { id: noticeText; anchors.centerIn: parent; width: parent.width - 40; elide: Text.ElideRight; text: root.notice; color: Tokens.text; font.pixelSize: 14 }
    }
    Timer { id: noticeTimer; interval: 5000; onTriggered: root.notice = "" }

    Timer { id: cursorTimer; interval: 5000; onTriggered: root.cursorHidden = true }   // el cursor se oculta a los 5 s si no hay controles
    Timer { id: hideTimer; interval: 30000; onTriggered: root.controlsVisible = false }

    Sidebar {
        id: sidebar
        z: 150                       // sobre las vistas, bajo la barra de título
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: root.navWidth
        visible: root.navWidth > 0
        clip: true
        compact: root.navCompact
        section: root.section
        onNavigate: (k) => { root.person = null; root.detail = null; root.section = k }
        onAboutRequested: aboutDialog.open()
        clientMode: root.clientMode
        serverName: root.serverName
        onDisconnectRequested: disconnectConfirm.open()
    }

    Modal {
        id: disconnectConfirm
        heading: "Desconectar"
        Column {
            width: parent.width; spacing: 16
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14; lineHeight: 1.35
                   text: "Dejarás de ver el catálogo de «" + root.serverName + "» y volverás a tu colección en este equipo. El servidor queda recordado para reconectarte cuando quieras; no se borra nada." }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: disconnectConfirm.close() }
                AdminButton { text: "Desconectar"; primary: true; onClicked: { disconnectConfirm.close(); root.person = null; root.detail = null; root.leaveClientMode() } }
            }
        }
    }

    // --- Barra de título propia (ventana sin marco) ---
    readonly property bool fullscreen: root.visibility === Window.FullScreen
    Rectangle {
        id: titleBar
        z: 200
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 36
        visible: (!root.playing || root.controlsVisible) && !root.fullscreen
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.playing ? "#E6000000" : "#B30E0D11" }
            GradientStop { position: 1.0; color: "#000E0D11" }
        }
        Row {
            anchors { left: parent.left; leftMargin: root.navWidth + 16; verticalCenter: parent.verticalCenter }
            spacing: 12
            Rectangle {   // botón de retroceso (solo icono)
                visible: root.playing || root.detail !== null
                width: 30; height: 26; radius: 6
                color: backHover.hovered ? "#33FFFFFF" : "transparent"
                NavIcon { anchors.centerIn: parent; width: 18; height: 18; name: "back"; color: backHover.hovered ? "white" : "#cccccc"; lineWidth: 2.2 }
                HoverHandler { id: backHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.playing ? root.closeMovie() : root.closeDetail() }
                Accessible.role: Accessible.Button; Accessible.name: "Volver"
            }
            Text { visible: root.playing || root.navWidth === 0; height: 26; verticalAlignment: Text.AlignVCenter; text: root.playing ? root.movieTitle : "CinemaLove"; color: "#dddddd"; font.pixelSize: 13; font.bold: true }
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
            z: 300
            readonly property bool resizable: !root.fullscreen && root.visibility !== Window.Maximized
            enabled: resizable
            visible: resizable   // maximizada o en pantalla completa no hay bordes ni cursor de redimensionado
            readonly property bool corner: (modelData.l || modelData.r) && (modelData.t || modelData.b)
            x: modelData.r ? root.width - width : 0
            y: modelData.b ? root.height - height : 0
            width: modelData.l || modelData.r ? 6 : root.width
            height: modelData.t || modelData.b ? 6 : root.height
            cursorShape: resizable ? modelData.c : Qt.ArrowCursor
            onPressed: root.startSystemResize(modelData.e)
        }
    }

    // --- Interfaz del reproductor (barra, OSD, error, fin, siguiente episodio) ---
    PlayerOverlay {
        id: overlay
        z: 50
        anchors.fill: parent
        visible: root.playing
        player: player
        title: root.movieTitle
        isEpisode: root.nowPlaying !== null && root.nowPlaying.kind === "episode"
        hasNext: root.nextEpisodeId !== ""
        nextTitle: root.nextEpisodeLabel
        backdropUrl: root.nowPlaying ? (root.nowPlaying.backdropUrl || "") : ""
        fullscreen: root.fullscreen
        controlsVisible: root.controlsVisible
        ended: root.ended
        onInteracted: hideTimer.restart()
        onSeeked: saveTimer.restart()
        onFocusRequested: player.forceActiveFocus()
        onCloseRequested: root.closeMovie()
        onFullscreenRequested: root.toggleFullscreen()
        onNextRequested: root.playNext()
        onRetryRequested: root.retry()
        onReplayRequested: root.replay()
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

    // Prueba de progreso: abre (reanudando si hay progreso), muestra posición, hace seek, cierra y deja la ficha refrescada.
    Timer { id: pt1; interval: 3000; onTriggered: { root.showDetail(0); const d = root.detail
        console.log("PT ficha: estado=" + d.progressState + " pos=" + d.progressPos + " audio=" + d.progressAudio + " sub=" + d.progressSub)
        root.openMovie(d, d.progressState === "enProgreso" ? d.progressPos : 0) } }
    Timer { id: pt2; interval: 11000; onTriggered: { console.log("PT tras reanudar: pos=" + player.position.toFixed(1) + " aid=" + player.audioId + " sid=" + player.subtitleId)
        player.seek(player.position + 300, false); saveTimer.restart() } }
    Timer { id: pt3; interval: 14500; onTriggered: { root.closeMovie()
        console.log("PT ficha tras cerrar: estado=" + root.detail.progressState + " pos=" + root.detail.progressPos) } }
    Timer { id: pt4; interval: 16000; onTriggered: { player.shutdown(); Qt.quit() } }
    Timer { id: fadeShotTimer; interval: 110; onTriggered: player.grabWindowTo(spikeArgs.catalogShot.replace(/\.png$/, "-mid.png")) }
    // Medición del desplazamiento con la rueda (--ui-test wheel): 1 muesca y luego 3 seguidas.
    property real wheelY0: 0
    Timer { id: wheelT1; interval: 1500; onTriggered: { console.log("WHEEL 1 muesca: " + (catalog.scrollY - root.wheelY0).toFixed(1) + " px"); root.wheelY0 = catalog.scrollY
        for (let i = 0; i < 3; i++) testInput.wheel(root, 700, 400, -120); wheelT2.start() } }
    Timer { id: wheelT2; interval: 1500; onTriggered: console.log("WHEEL 3 muescas: " + (catalog.scrollY - root.wheelY0).toFixed(1) + " px") }
    Timer { id: uiTestTimer; interval: 5000; onTriggered: {
        if (spikeArgs.uiTest === "page2") { movies.page = 1; console.log("PAGE2 primera=" + movies.get(0).title + " rows=" + movies.rowCount() + " paginas=" + movies.pageCount); root.showDetail(0) }
        else if (spikeArgs.uiTest === "person") root.openPerson(root.detail.cast[0])
        else if (spikeArgs.uiTest === "pl-ended") root.ended = true
        else if (spikeArgs.uiTest === "pl-audio") overlay.openAudioMenu(overlay.audioButton)
        else if (spikeArgs.uiTest === "pl-subs") overlay.openSubMenu(overlay.subButton)
        else if (spikeArgs.uiTest === "pl-subcfg") overlay.openSubSettings()
        else if (spikeArgs.uiTest === "pl-info") overlay.openInfo()
        else if (spikeArgs.uiTest === "pl-osd") { overlay.flashSkip(10); overlay.flashSkip(10); overlay.flashVolume() }
        else if (spikeArgs.uiTest === "pl-nextcard") player.seek(player.duration - 19, false)
        else if (spikeArgs.uiTest === "pl-error") player.load("D:/no-existe-esto.mkv", 0)
        else if (spikeArgs.uiTest === "page2list") { movies.page = 1 }
        else if (spikeArgs.uiTest === "page6") { movies.page = 5; console.log("PAGE6 filas=" + movies.rowCount()) }
        else if (spikeArgs.uiTest === "wheel") { root.wheelY0 = catalog.scrollY; testInput.wheel(root, 700, 400, -120); wheelT1.start() }
        else if (spikeArgs.uiTest === "fade") { root.section = "series"; fadeShotTimer.start() }
        else if (spikeArgs.uiTest === "wizard0") { root.wizardActive = true; startup.step = 0 }
        else if (spikeArgs.uiTest === "wizard1") { root.wizardActive = true; startup.step = 1; startup.chosen = spikeArgs.dataDir + "/../bk-test" }
        else if (spikeArgs.uiTest === "wizard1locked") { root.wizardActive = true; startup.step = 1; startup.chosen = spikeArgs.dataDir }
        else if (spikeArgs.uiTest === "wizard1new") { root.wizardActive = true; startup.step = 1; startup.chosen = spikeArgs.dataDir + "/../carpeta-nueva-prueba" }
        else if (spikeArgs.uiTest === "wizardflow") {   // lo mismo que «Abrir esta colección»
            root.wizardActive = true; startup.step = 1; startup.chosen = spikeArgs.dataDir + "/../bk-test"
            if (dataFolder.apply(startup.chosen)) { startup.step = 2; backend.restart(dataFolder.path) } }
        else if (spikeArgs.uiTest === "wizard5") { root.wizardActive = true; startup.step = 5 }
        else if (spikeArgs.uiTest === "collection") collectionsView.open(collectionsView.collections[0])
        else if (spikeArgs.uiTest === "special") collectionsView.open({ special: true })
        else if (spikeArgs.uiTest === "menu") root.openItemMenu({ id: movies.get(2).id, kind: "movie", title: movies.get(2).title }, 420, 330)
        else if (spikeArgs.uiTest === "about") aboutDialog.open()
        else if (spikeArgs.uiTest === "orphans") adminView.testOrphans()
        else if (spikeArgs.uiTest === "tmdb") adminView.testTmdb()
        else if (spikeArgs.uiTest === "markconfirm") root.confirmMark("x", 3, "noVisto", "Banshee") } }
    Timer { id: adminScanTimer; interval: 4500; onTriggered: adminView.startJob("/escanear-todo", null, "Escaneo iniciado.") }
    Timer { id: homeScrollTimer; interval: 5000; onTriggered: { if (root.seriesOpen) seriesDetailView.scrollTo(spikeArgs.homeScroll); else homeView.scrollTo(spikeArgs.homeScroll) } }
    Timer { id: openTimer; interval: spikeArgs.clientAuto ? 8000 : 3000; onTriggered: {
        if (root.section === "series") {
            root.showSeriesAt(0)
            if (spikeArgs.play) root.playEpisodeById(root.detail.next.episode.id)
        } else if (spikeArgs.play) root.openMovie(movies.get(0), 0); else root.showDetail(0) } }
    Timer { id: shotTimer; interval: spikeArgs.openFirst ? (spikeArgs.clientAuto ? 22000 : 14000) : 9000; onTriggered: {
        player.grabWindowTo(spikeArgs.catalogShot); if (root.playing) root.closeMovie(); player.shutdown(); Qt.quit() } }

    Component.onCompleted: {
        if (spikeArgs.progressTest) { if (spikeArgs.search) catalog.setSearch(spikeArgs.search); pt1.start(); pt2.start(); pt3.start(); pt4.start() }
        if (spikeArgs.adminScan) adminScanTimer.start()
        if (spikeArgs.uiTest) uiTestTimer.start()
        if (spikeArgs.homeScroll > 0) Qt.callLater(() => homeScrollTimer.start())
        if (spikeArgs.catalogShot) { if (spikeArgs.search) root.currentCatalog().setSearch(spikeArgs.search); shotTimer.start(); if (spikeArgs.openFirst) openTimer.start() }
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

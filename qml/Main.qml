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
    property bool playing: false          // true cuando se está viendo una película (si no, se muestra el catálogo)
    property string movieTitle: ""
    property string notice: ""
    // Sección principal: «home» (Inicio), «catalog» (Películas), «series», «collections» o «admin» (Administración). Las pruebas del catálogo arrancan en «catalog».
    property string section: spikeArgs.section || ((spikeArgs.catalogShot || spikeArgs.progressTest || spikeArgs.openFirst) ? "catalog" : "home")
    property var detail: null   // ficha abierta (película de MovieModel o serie de SeriesCatalog, con kind === "series") o null
    readonly property bool seriesOpen: detail !== null && detail.kind === "series"
    readonly property bool movieOpen: detail !== null && detail.kind !== "series"

    // Los modelos leen la base solo cuando la colección está abierta: el backend (respaldo, bloqueo, migraciones) va primero.
    readonly property bool wizardMode: wizardActive
    property bool wizardActive: dataFolder.path === ""
    readonly property bool dataReady: dataFolder.path !== "" && (spikeArgs.noBackend || !spikeArgs.dataDir || backend.state === "listo")
    readonly property string dbFile: dataReady ? dataFolder.dbFile : ""
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
    }
    function markWatched() {
        if (!nowPlaying || watchedMarked) return
        watchedMarked = true
        store.saveProgress(nowPlaying.id, 0, "visto")
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
    function closeDetail() { detail = null; catalog.forceActiveFocus() }

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
        movies.reload(); seriesCat.reload()
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
                { label: "Marcar serie como vista", run: () => confirmMark(it.id, null, "visto", it.title) },
                { label: "Marcar serie como no vista", run: () => confirmMark(it.id, null, "noVisto", it.title) }
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
                    { label: us.enProximas ? "Quitar de Próximas a ver" : "Agregar a Próximas a ver",
                      run: () => mutate(us.enProximas ? "DELETE" : "PUT", "/api/v1/watchlist/" + id, null, us.enProximas ? "Quitada de Próximas a ver." : "Agregada a Próximas a ver.") },
                    { label: seen ? "Marcar como no vista" : "Marcar como vista",
                      run: () => mutate("PUT", "/api/v1/media/" + id + "/progress", { estado: seen ? "noVisto" : "visto" }, seen ? "Marcada como no vista." : "Marcada como vista.") }
                ]
                if (list.length) entries.push({ header: "COLECCIONES" })
                list.forEach(c => {
                    const inC = inCols.indexOf(c.id) >= 0
                    entries.push({ label: (inC ? "Quitar de «" : "Agregar a «") + c.nombre + "»",
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
        anchors.fill: parent
        focus: true
        onEndOfFile: (reason) => {
            if (reason !== "eof" || spikeArgs.selftest) return
            const cur = root.nowPlaying
            root.markWatched()
            const nextId = cur && cur.kind === "episode" ? seriesCat.nextEpisodeId(cur.id) : ""
            if (nextId) { // reproduce el siguiente episodio de la serie sin volver a la ficha
                root.refreshDetailAfter("episode", cur.id)
                root.playEpisodeById(nextId, 0)
            } else root.closeMovie()
        }

        Keys.onPressed: (e) => {
            switch (e.key) {
            case Qt.Key_Space: togglePause(); break
            case Qt.Key_Right: seek(e.modifiers & Qt.ShiftModifier ? 30 : 10, true); saveTimer.restart(); break
            case Qt.Key_Left: seek(e.modifiers & Qt.ShiftModifier ? -30 : -10, true); saveTimer.restart(); break
            case Qt.Key_Up: volume = volume + 5; break
            case Qt.Key_Down: volume = volume - 5; break
            case Qt.Key_F: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen; break
            case Qt.Key_Escape: if (root.visibility === Window.FullScreen) root.visibility = Window.Windowed; else root.closeMovie(); break
            case Qt.Key_Backspace: root.closeMovie(); break
            case Qt.Key_A: { const n = root.cycle(audioTracks, audioId, false); if (n) setAudio(n); saveTimer.restart(); break }
            case Qt.Key_S: { const n = root.cycle(subtitleTracks, subtitleId, true); if (n) setSubtitle(n); saveTimer.restart(); break }
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
        visible: !root.playing && !root.detail && root.section === "catalog"
        model: movies
        total: movies.total
        error: movies.error
        onSearch: (t) => movies.filter = t
        onMenuRequested: (it, x, y) => root.openItemMenu(it, x, y)
        onMovieChosen: (row) => root.showDetail(row)
    }

    CatalogView {
        id: seriesCatalog
        anchors.fill: parent
        visible: !root.playing && !root.detail && root.section === "series"
        model: seriesCat
        total: seriesCat.total
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
        anchors.fill: parent
        visible: !root.playing && !root.detail && root.section === "admin"
        api: api
        backendState: root.backendNote !== "" ? "error" : backend.state
        backendError: root.backendNote !== "" ? root.backendNote : backend.error
        onToast: (t) => { root.notice = t; noticeTimer.restart() }
        onCatalogChanged: { movies.reload(); seriesCat.reload() }
        onChangeFolder: { dataFolder.forget(); backend.stop(); root.wizardActive = true; startup.step = 1 }
    }

    CollectionsView {
        id: collectionsView
        anchors.fill: parent
        visible: !root.playing && !root.detail && root.section === "collections"
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
        visible: root.wizardActive || !root.dataReady
        onFinished: { root.wizardActive = false; movies.reload(); seriesCat.reload() }
        onChooseAnotherFolder: { dataFolder.forget(); backend.stop(); root.wizardActive = true; startup.step = 1 }
    }

    HomeView {
        id: homeView
        anchors.fill: parent
        visible: !root.playing && !root.detail && root.section === "home"
        model: movies
        series: seriesCat
        onOpenDetail: (it) => root.openItemDetail(it)
        onMenuRequested: (it, x, y) => root.openItemMenu(it, x, y)
        onPlayMovie: (it) => root.playItem(it)
    }

    SeriesDetailView {
        id: seriesDetailView
        anchors.fill: parent
        visible: !root.playing && root.seriesOpen
        series: root.seriesOpen ? root.detail : ({})
        catalog: seriesCat
        onBack: root.closeDetail()
        onPlayEpisode: (id, start) => root.playEpisodeById(id, start)
        onMarkRequested: (sid, season, estado) => root.confirmMark(sid, season, estado, root.detail.title)
    }

    DetailView {
        id: detailView
        anchors.fill: parent
        visible: !root.playing && root.movieOpen
        movie: root.movieOpen ? root.detail : ({})
        onBack: root.closeDetail()
        onPlay: (start) => root.openMovie(root.detail, start)
    }

    Rectangle { // aviso temporal (archivo no disponible)
        visible: root.notice !== "" && !root.playing
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 32 }
        width: Math.min(parent.width - 64, noticeText.implicitWidth + 40); height: 44; radius: Tokens.radius
        color: Tokens.surfaceRaised; border.color: Tokens.borderStrong
        Text { id: noticeText; anchors.centerIn: parent; width: parent.width - 40; elide: Text.ElideRight; text: root.notice; color: Tokens.text; font.pixelSize: 14 }
    }
    Timer { id: noticeTimer; interval: 5000; onTriggered: root.notice = "" }

    Timer { id: hideTimer; interval: 30000; onTriggered: root.controlsVisible = false }

    // --- Barra de título propia (ventana sin marco) ---
    readonly property bool fullscreen: root.visibility === Window.FullScreen
    Rectangle {
        id: titleBar
        z: 200
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
                visible: root.playing || root.detail !== null
                text: root.playing ? "‹ " + (root.detail ? "Ficha" : "Biblioteca") : (root.seriesOpen ? "‹ Series" : "‹ Películas"); color: backHover.hovered ? "white" : "#bbbbbb"; font.pixelSize: 13
                HoverHandler { id: backHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.playing ? root.closeMovie() : root.closeDetail() }
            }
            Text { text: root.playing ? root.movieTitle : "CinemaLove"; color: "#dddddd"; font.pixelSize: 13; font.bold: true }
            Repeater {
                model: root.playing || root.detail || startup.visible ? [] : [ { k: "home", t: "Inicio" }, { k: "catalog", t: "Películas" }, { k: "series", t: "Series" }, { k: "collections", t: "Colecciones" }, { k: "admin", t: "Administración" } ]
                delegate: Text {
                    leftPadding: 12
                    text: modelData.t; font.pixelSize: 13; font.weight: Font.Medium
                    color: root.section === modelData.k ? Tokens.text : (navHover.hovered ? Tokens.text : Tokens.textMuted)
                    HoverHandler { id: navHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.section = modelData.k }
                }
            }
        }
        DragHandler { target: null; onActiveChanged: if (active) root.startSystemMove() }
        TapHandler { acceptedButtons: Qt.LeftButton; onDoubleTapped: root.visibility = root.visibility === Window.Maximized ? Window.Windowed : Window.Maximized }
        Row {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            Rectangle {
                visible: !root.playing
                width: 46; height: titleBar.height; color: aboutHover.hovered ? "#33ffffff" : "transparent"
                Text { anchors.centerIn: parent; text: "ⓘ"; color: "white"; font.pixelSize: 15 }
                HoverHandler { id: aboutHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: aboutDialog.open() }
                Accessible.role: Accessible.Button; Accessible.name: "Acerca de"
            }
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
                onClicked: (m) => { if (player.duration > 0) { player.seek(player.duration * m.x / width, false); saveTimer.restart() } }
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

    // Prueba de progreso: abre (reanudando si hay progreso), muestra posición, hace seek, cierra y deja la ficha refrescada.
    Timer { id: pt1; interval: 3000; onTriggered: { root.showDetail(0); const d = root.detail
        console.log("PT ficha: estado=" + d.progressState + " pos=" + d.progressPos + " audio=" + d.progressAudio + " sub=" + d.progressSub)
        root.openMovie(d, d.progressState === "enProgreso" ? d.progressPos : 0) } }
    Timer { id: pt2; interval: 11000; onTriggered: { console.log("PT tras reanudar: pos=" + player.position.toFixed(1) + " aid=" + player.audioId + " sid=" + player.subtitleId)
        player.seek(player.position + 300, false); saveTimer.restart() } }
    Timer { id: pt3; interval: 14500; onTriggered: { root.closeMovie()
        console.log("PT ficha tras cerrar: estado=" + root.detail.progressState + " pos=" + root.detail.progressPos) } }
    Timer { id: pt4; interval: 16000; onTriggered: { player.shutdown(); Qt.quit() } }
    Timer { id: uiTestTimer; interval: 5000; onTriggered: {
        if (spikeArgs.uiTest === "wizard0") { root.wizardActive = true; startup.step = 0 }
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
    Timer { id: openTimer; interval: 3000; onTriggered: {
        if (root.section === "series") {
            root.showSeriesAt(0)
            if (spikeArgs.play) root.playEpisodeById(root.detail.next.episode.id)
        } else if (spikeArgs.play) root.openMovie(movies.get(0), 0); else root.showDetail(0) } }
    Timer { id: shotTimer; interval: spikeArgs.openFirst ? 14000 : 9000; onTriggered: {
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

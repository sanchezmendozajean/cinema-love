import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import CinemaLove

// Administración (Fase 4, versión inicial): bibliotecas (alta, escaneo, metadata, baja), tareas en segundo plano y clave de TMDb.
// Todo es UI sobre los endpoints /api/v1/admin/* del backend.
Rectangle {
    id: view
    color: Tokens.bg
    property var api                       // ApiClient
    property string backendState: "detenido"
    property string backendError: ""
    signal catalogChanged()                // un trabajo terminó y cambió el catálogo: recargar modelos
    signal toast(string text)

    property string tab: "bibliotecas"
    property var libraries: []
    property var jobs: []
    property var tmdb: ({ configurada: false, mascara: "" })
    property bool anyRunning: false

    readonly property bool ready: view.backendState === "listo"

    // --- datos ---
    function call(method, path, body, cb) { api.request(method, "/api/v1/admin" + path, body === undefined ? null : body, cb) }
    function loadLibraries() { call("GET", "/bibliotecas", undefined, (st, d) => { if (st === 200) libraries = d }) }
    function loadTmdb() { call("GET", "/ajustes/tmdb", undefined, (st, d) => { if (st === 200) tmdb = d }) }
    function loadJobs() {
        call("GET", "/jobs", undefined, (st, d) => {
            if (st !== 200) return
            const wasRunning = anyRunning
            jobs = d.slice().sort((a, b) => b.creadoEn - a.creadoEn)
            anyRunning = d.some(j => j.estado === "corriendo")
            if (wasRunning && !anyRunning) { loadLibraries(); view.catalogChanged() }
        })
    }
    function refreshAll() { loadLibraries(); loadJobs(); loadTmdb() }
    function startJob(path, body, okMsg) {
        call("POST", path, body, (st, d, err) => {
            if (st >= 200 && st < 300) { toast(okMsg); tab = "tareas"; loadJobs() }
            else toast(err || "No se pudo iniciar la tarea.")
        })
    }

    onReadyChanged: if (ready) refreshAll()
    onVisibleChanged: if (visible && ready) refreshAll()
    Timer { interval: 1200; repeat: true; running: view.visible && view.ready && (view.anyRunning || view.tab === "tareas"); onTriggered: view.loadJobs() }

    // --- cabecera ---
    Column {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 48; leftMargin: 40; rightMargin: 40 }
        spacing: 4
        Text { text: "AJUSTES"; color: Tokens.accentSoft; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
        Text { text: "Administración"; color: Tokens.text; font.pixelSize: 40; font.family: "Georgia"; font.weight: Font.DemiBold }
        Row {
            spacing: 4; topPadding: 16
            Repeater {
                model: [ { k: "bibliotecas", t: "Bibliotecas" }, { k: "tareas", t: "Tareas" + (view.anyRunning ? "  ●" : "") }, { k: "tmdb", t: "TMDb" } ]
                delegate: Rectangle {
                    height: 36; width: lbl.width + 32; radius: Tokens.radiusSmall
                    color: view.tab === modelData.k ? "#24E23F52" : (th.hovered ? Tokens.surfaceHover : "transparent")
                    Text { id: lbl; anchors.centerIn: parent; text: modelData.t; font.pixelSize: 14; font.weight: Font.Medium
                           color: view.tab === modelData.k ? Tokens.accentSoft : Tokens.textMuted }
                    HoverHandler { id: th; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: view.tab = modelData.k }
                }
            }
        }
    }

    // Estado del backend
    Column {
        anchors.centerIn: parent; spacing: 10
        visible: !view.ready
        Text { anchors.horizontalCenter: parent.horizontalCenter; color: Tokens.text; font.pixelSize: 20; font.weight: Font.DemiBold
               text: view.backendState === "error" ? "No se pudo iniciar el servicio de administración" : "Iniciando el servicio de administración…" }
        Text { anchors.horizontalCenter: parent.horizontalCenter; width: Math.min(560, view.width - 80); horizontalAlignment: Text.AlignHCenter
               wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; visible: text !== ""; text: view.backendError }
        BusyIndicator { visible: view.backendState === "iniciando"; anchors.horizontalCenter: parent.horizontalCenter; running: visible }
    }

    // --- Bibliotecas ---
    Flickable {
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 24; leftMargin: 40; rightMargin: 40 }
        visible: view.ready && view.tab === "bibliotecas"
        contentHeight: libCol.height + 40
        clip: true; boundsBehavior: Flickable.StopAtBounds
        Column {
            id: libCol
            width: parent.width; spacing: 14
            Row {
                spacing: 10
                AdminButton { text: "＋  Nueva biblioteca"; primary: true; onClicked: newLib.open() }
                AdminButton { text: "Escanear todo"; enabled: view.libraries.length > 0; onClicked: view.startJob("/escanear-todo", null, "Escaneo de todas las bibliotecas iniciado.") }
            }
            Text { visible: view.libraries.length === 0; color: Tokens.textMuted; font.pixelSize: 14; text: "Aún no hay bibliotecas. Crea una para empezar." }
            Repeater {
                model: view.libraries
                delegate: Rectangle {
                    required property var modelData
                    width: libCol.width; height: 124; radius: Tokens.radius
                    color: Tokens.surface; border.color: Tokens.border
                    Column {
                        anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 16; right: actions.left; rightMargin: 16 }
                        spacing: 6
                        Row {
                            spacing: 10
                            Text { text: modelData.nombre; color: Tokens.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                            Rectangle { height: 22; width: tipoT.width + 16; radius: 11; color: "#24E23F52"; anchors.verticalCenter: parent.verticalCenter
                                Text { id: tipoT; anchors.centerIn: parent; text: modelData.tipo === "serie" ? "Series" : "Películas"; color: Tokens.accentSoft; font.pixelSize: 11; font.weight: Font.DemiBold } }
                            Rectangle { height: 22; width: dispT.width + 16; radius: 11; anchors.verticalCenter: parent.verticalCenter
                                color: modelData.disponible ? "#1F4CAF7D" : "#26E5484D"
                                Text { id: dispT; anchors.centerIn: parent; text: modelData.disponible ? "Disponible" : "No disponible"
                                       color: modelData.disponible ? "#4CAF7D" : Tokens.danger; font.pixelSize: 11; font.weight: Font.DemiBold } }
                        }
                        Text { text: modelData.ruta; color: Tokens.textMuted; font.pixelSize: 13; elide: Text.ElideMiddle; width: parent.width }
                        Text { color: Tokens.textFaint; font.pixelSize: 13
                               text: (modelData.tipo === "serie" ? modelData.series + " series · " + modelData.items + " episodios" : modelData.items + " películas")
                                     + (modelData.huerfanos > 0 ? "  ·  " + modelData.huerfanos + " sin archivo" : "") }
                    }
                    Row {
                        id: actions
                        anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 8
                        AdminButton { text: "Escanear"; enabled: modelData.disponible
                            onClicked: view.startJob("/bibliotecas/" + modelData.id + "/escanear", null, "Escaneo de «" + modelData.nombre + "» iniciado.") }
                        AdminButton { text: "Metadata ▾"; onClicked: metaMenu.popup() 
                            Menu {
                                id: metaMenu
                                MenuItem { text: "Buscar solo lo que falta"; onTriggered: view.startJob("/bibliotecas/" + modelData.id + "/metadata", { modo: "faltantes" }, "Búsqueda de metadata iniciada.") }
                                MenuItem { text: "Volver a buscar todo"; onTriggered: view.startJob("/bibliotecas/" + modelData.id + "/metadata", { modo: "todo" }, "Búsqueda de metadata iniciada.") }
                            } }
                        AdminButton { text: "Eliminar…"; danger: true; onClicked: { delLib.target = modelData; delLib.open() } }
                    }
                }
            }
        }
    }

    // --- Tareas ---
    Flickable {
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 24; leftMargin: 40; rightMargin: 40 }
        visible: view.ready && view.tab === "tareas"
        contentHeight: jobCol.height + 40
        clip: true; boundsBehavior: Flickable.StopAtBounds
        Column {
            id: jobCol
            width: parent.width; spacing: 12
            Text { visible: view.jobs.length === 0; color: Tokens.textMuted; font.pixelSize: 14; text: "No hay tareas en esta sesión." }
            Repeater {
                model: view.jobs
                delegate: Rectangle {
                    required property var modelData
                    width: jobCol.width; height: 92; radius: Tokens.radius; color: Tokens.surface; border.color: Tokens.border
                    readonly property bool running: modelData.estado === "corriendo"
                    Column {
                        anchors { left: parent.left; leftMargin: 20; right: cancelBtn.left; rightMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 8
                        Text { text: modelData.titulo; color: Tokens.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight; width: parent.width }
                        Rectangle { visible: running; width: parent.width; height: 5; radius: 2; color: Tokens.border
                            Rectangle { height: parent.height; radius: 2; color: Tokens.accent
                                width: modelData.progreso && modelData.progreso.total > 0 ? parent.width * modelData.progreso.hechos / modelData.progreso.total : parent.width * 0.15 } }
                        Text { width: parent.width; elide: Text.ElideRight; font.pixelSize: 13
                               color: modelData.estado === "error" ? Tokens.danger : (modelData.estado === "completado" ? "#4CAF7D" : Tokens.textMuted)
                               text: running ? (modelData.progreso ? modelData.progreso.hechos + " de " + modelData.progreso.total : "En curso…")
                                     : modelData.estado === "completado" ? "Completado · " + view.summary(modelData.resultado)
                                     : modelData.estado === "cancelado" ? "Cancelado" : (modelData.error || "Error") }
                    }
                    AdminButton { id: cancelBtn; visible: running; text: "Cancelar"; anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                        onClicked: view.call("POST", "/jobs/" + modelData.id + "/cancelar", null, () => view.loadJobs()) }
                }
            }
        }
    }

    function summary(r) {
        if (!r) return "sin cambios"
        if (r.nuevos !== undefined) return r.nuevos + " nuevos · " + r.reconciliados + " reconciliados · " + r.noEncontrados + " sin archivo"
        if (r.total !== undefined) return r.resueltos + " de " + r.total + " resueltos" + (r.ambiguos && r.ambiguos.length ? " · " + r.ambiguos.length + " ambiguos" : "")
                                          + (r.sinCoincidencia && r.sinCoincidencia.length ? " · " + r.sinCoincidencia.length + " sin coincidencia" : "")
        if (r.bibliotecas) return r.bibliotecas.map(b => b.biblioteca + ": " + (b.nuevos || 0) + " nuevos, " + (b.reconciliados || 0) + " reconciliados, "
                                                          + (b.noEncontrados || 0) + " sin archivo").join("  ·  ")
        return "listo"
    }

    // --- TMDb ---
    Column {
        anchors { left: parent.left; top: header.bottom; topMargin: 28; leftMargin: 40 }
        width: Math.min(560, view.width - 80); spacing: 14
        visible: view.ready && view.tab === "tmdb"
        Text { text: "Clave de la API de TMDb"; color: Tokens.text; font.pixelSize: 18; font.weight: Font.DemiBold }
        Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14
               text: "Se usa para descargar pósters, sinopsis y reparto. Se guarda solo en tu base de datos local." }
        Text { color: view.tmdb.configurada ? Tokens.success : Tokens.warning; font.pixelSize: 14
               text: view.tmdb.configurada ? "Configurada · " + (view.tmdb.mascara || "") : "Aún no hay una clave configurada." }
        TextField {
            id: keyField
            width: parent.width; height: 44
            placeholderText: "Pega aquí tu clave (v3)"; placeholderTextColor: Tokens.textFaint
            echoMode: TextInput.Password; color: Tokens.text; font.pixelSize: 14; leftPadding: 14
            background: Rectangle { radius: Tokens.radius; color: Tokens.surface; border.width: keyField.activeFocus ? 2 : 1; border.color: keyField.activeFocus ? Tokens.accent : Tokens.border }
        }
        Row {
            spacing: 10
            AdminButton { text: "Probar"; enabled: keyField.text.length > 0 || view.tmdb.configurada
                onClicked: view.call("POST", "/ajustes/tmdb/probar", { apiKey: keyField.text }, (st, d, err) => view.toast(st === 200 ? "La clave funciona." : (err || "La clave no es válida."))) }
            AdminButton { text: "Guardar"; primary: true; enabled: keyField.text.length > 0
                onClicked: view.call("PUT", "/ajustes/tmdb", { apiKey: keyField.text }, (st, d, err) => {
                    if (st === 200) { view.tmdb = d; keyField.text = ""; view.toast("Clave guardada.") } else view.toast(err || "No se pudo guardar.") }) }
            AdminButton { text: "Quitar clave"; danger: true; visible: view.tmdb.configurada
                onClicked: view.call("PUT", "/ajustes/tmdb", { apiKey: null }, (st, d) => { if (st === 200) { view.tmdb = d; view.toast("Clave eliminada.") } }) }
        }
    }

    // --- Diálogos ---
    Modal {
        id: newLib
        heading: "Nueva biblioteca"
        onOpened: { nameField.text = ""; pathField.text = ""; typeSel.value = "pelicula"; nameField.forceActiveFocus() }
        Column {
            width: parent.width; spacing: 14
            Text { text: "Nombre"; color: Tokens.textMuted; font.pixelSize: 12 }
            TextField { id: nameField; width: parent.width; height: 42; color: Tokens.text; font.pixelSize: 14; leftPadding: 12; placeholderText: "Películas"; placeholderTextColor: Tokens.textFaint
                background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: nameField.activeFocus ? Tokens.accent : Tokens.border } }
            Text { text: "Tipo"; color: Tokens.textMuted; font.pixelSize: 12 }
            Row {
                id: typeSel
                property string value: "pelicula"
                spacing: 8
                Repeater {
                    model: [ { k: "pelicula", t: "Películas" }, { k: "serie", t: "Series" } ]
                    delegate: Rectangle {
                        width: 130; height: 40; radius: Tokens.radiusSmall
                        color: typeSel.value === modelData.k ? "#24E23F52" : Tokens.surface
                        border.color: typeSel.value === modelData.k ? Tokens.accent : Tokens.border
                        Text { anchors.centerIn: parent; text: modelData.t; font.pixelSize: 14; color: typeSel.value === modelData.k ? Tokens.accentSoft : Tokens.textMuted }
                        TapHandler { onTapped: typeSel.value = modelData.k }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }
            Text { text: "Carpeta"; color: Tokens.textMuted; font.pixelSize: 12 }
            Row {
                spacing: 8; width: parent.width
                TextField { id: pathField; width: parent.width - 108; height: 42; color: Tokens.text; font.pixelSize: 14; leftPadding: 12; placeholderText: "D:\\Cine\\Películas"; placeholderTextColor: Tokens.textFaint
                    background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: pathField.activeFocus ? Tokens.accent : Tokens.border } }
                AdminButton { text: "Elegir…"; onClicked: folderDialog.open() }
            }
            Text { id: newLibErr; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; visible: text !== "" }
            Row {
                anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: newLib.close() }
                AdminButton { text: "Crear y escanear"; primary: true; enabled: nameField.text.trim() !== "" && pathField.text.trim() !== ""
                    onClicked: view.call("POST", "/bibliotecas", { nombre: nameField.text.trim(), tipo: typeSel.value, ruta: pathField.text.trim(), escanear: true }, (st, d, err) => {
                        if (st === 201 || st === 200) { newLib.close(); view.toast("Biblioteca creada; escaneando…"); view.tab = "tareas"; view.refreshAll() }
                        else newLibErr.text = err || "No se pudo crear la biblioteca." }) }
            }
        }
        FolderDialog { id: folderDialog; title: "Elegir la carpeta de la biblioteca"; onAccepted: pathField.text = selectedFolder.toString().replace(/^file:\/\/\//, "").replace(/\//g, "\\") }
    }

    Modal {
        id: delLib
        property var target: ({ nombre: "" })
        heading: "Eliminar biblioteca"
        onOpened: { confirmField.text = ""; delErr.text = "" }
        Column {
            width: parent.width; spacing: 14
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14; lineHeight: 1.35
                   text: "Se quitará «" + delLib.target.nombre + "» del catálogo, junto con su progreso e historial. Tus archivos de video NO se tocan." }
            Text { text: "Para confirmar, escribe el nombre exacto de la biblioteca:"; color: Tokens.textMuted; font.pixelSize: 13 }
            TextField { id: confirmField; width: parent.width; height: 42; color: Tokens.text; font.pixelSize: 14; leftPadding: 12; placeholderText: delLib.target.nombre; placeholderTextColor: Tokens.textFaint
                background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: confirmField.activeFocus ? Tokens.danger : Tokens.border } }
            Text { id: delErr; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; visible: text !== "" }
            Row {
                anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: delLib.close() }
                AdminButton { text: "Eliminar"; danger: true; primary: true; enabled: confirmField.text === delLib.target.nombre
                    onClicked: view.call("DELETE", "/bibliotecas/" + delLib.target.id + "?confirm=" + encodeURIComponent(confirmField.text), null, (st, d, err) => {
                        if (st === 200) { delLib.close(); view.toast("Biblioteca eliminada."); view.loadLibraries(); view.catalogChanged() }
                        else delErr.text = err || "No se pudo eliminar." }) }
            }
        }
    }
}

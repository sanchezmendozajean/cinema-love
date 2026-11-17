import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import CinemaLove

// Arranque: asistente de bienvenida (primera vez o «usar otra carpeta»), pantalla «abriendo tu colección» y pantalla de error.
// Política de datos: antes de abrir una colección existente el backend hace una copia de seguridad automática de
// cinemalove.sqlite y toma el bloqueo de un solo escritor (compartido con las otras versiones de CinemaLove).
Rectangle {
    id: view
    color: Tokens.bg
    property var api                       // ApiClient
    property bool wizard: false            // true: asistente; false: solo apertura/errores
    property int step: 0                   // 0 bienvenida · 1 carpeta · 2 abriendo · 3 TMDb · 4 biblioteca · 5 listo
    signal finished()
    signal chooseAnotherFolder()

    property string chosen: ""
    property var info: ({})
    readonly property bool opening: !wizard && backend.state !== "error"
    readonly property bool failed: backend.state === "error"
    readonly property var stepNames: ["Bienvenida", "Datos", "Abriendo", "TMDb", "Biblioteca", "Listo"]

    function inspectChosen() { info = chosen !== "" ? dataFolder.inspect(chosen) : ({}) }
    onChosenChanged: inspectChosen()
    onWizardChanged: if (wizard) { step = 0; chosen = ""; info = ({}) }

    // Avanza según el resultado de abrir la colección
    Connections {
        target: backend
        function onStateChanged() {
            if (!view.wizard || view.step !== 2) return
            if (backend.state === "listo") view.step = (backend.info.bibliotecas > 0) ? 5 : 3
        }
    }

    function errorTitle() {
        switch (backend.errorCode) {
        case "bloqueada": return "Tu colección está en uso"
        case "version-antigua": return "La base es de una versión antigua"
        default: return "No se pudo abrir tu colección"
        }
    }
    function errorBody() {
        if (backend.errorCode === "bloqueada")
            return "Parece que CinemaLove (esta u otra de sus versiones: Electron, Flutter…) tiene abiertos estos datos. Para no arriesgarlos solo puede haber una aplicación a la vez. Ciérrala y vuelve a intentarlo."
        return backend.error
    }

    // Fondo sutil de acento
    Rectangle { anchors.fill: parent; gradient: Gradient { orientation: Gradient.Vertical
        GradientStop { position: 0.0; color: "#1A9519AE" } GradientStop { position: 0.55; color: Tokens.bg } } opacity: 0.0 }
    Rectangle { anchors.fill: parent; gradient: Gradient {
        GradientStop { position: 0.0; color: "#14E23F52" } GradientStop { position: 0.6; color: "#000E0D11" } } }

    // ===== Apertura (sin asistente) =====
    Column {
        anchors.centerIn: parent; spacing: 18
        visible: view.opening
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "CinemaLove"; color: Tokens.text; font.pixelSize: 44; font.family: Tokens.fontDisplay; font.weight: Font.DemiBold }
        BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: view.opening; palette.dark: Tokens.accent }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Abriendo tu colección…"; color: Tokens.textMuted; font.pixelSize: 15 }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: dataFolder.path; color: Tokens.textFaint; font.pixelSize: 12; elide: Text.ElideMiddle; width: 520; horizontalAlignment: Text.AlignHCenter }
    }

    // ===== Error de apertura (sin asistente) =====
    Rectangle {
        anchors.centerIn: parent
        visible: !view.wizard && view.failed
        width: Math.min(620, view.width - 48); height: errCol.height + 56
        radius: Tokens.radius + 4; color: Tokens.surfaceRaised; border.color: Tokens.borderStrong
        Column {
            id: errCol
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28 }
            spacing: 14
            Text { text: view.errorTitle(); color: Tokens.text; font.pixelSize: 22; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; width: parent.width }
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.35; text: view.errorBody() }
            Text { width: parent.width; wrapMode: Text.WrapAnywhere; color: Tokens.textFaint; font.pixelSize: 12; text: dataFolder.path }
            Row { spacing: 10; anchors.right: parent.right; topPadding: 6
                AdminButton { text: "Elegir otra carpeta…"; onClicked: view.chooseAnotherFolder() }
                AdminButton { text: "Reintentar"; primary: true; onClicked: backend.restart(dataFolder.path) }
            }
        }
    }

    // ===== Asistente =====
    Rectangle {
        id: card
        anchors.centerIn: parent
        visible: view.wizard
        width: Math.min(680, view.width - 48); height: Math.min(view.height - 40, cardCol.height + 56)
        radius: Tokens.radius + 6; color: Tokens.surfaceRaised; border.color: Tokens.borderStrong
        clip: true

        Column {
            id: cardCol
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 32 }
            spacing: 18

            // Indicador de pasos: Datos · TMDb · Biblioteca · Listo
            Row {
                spacing: 8
                visible: view.step > 0
                readonly property int current: view.step <= 2 ? 0 : view.step - 2
                Repeater {
                    model: 4
                    delegate: Rectangle { width: index === parent.current ? 28 : 8; height: 8; radius: 4
                        color: index <= parent.current ? Tokens.accent : Tokens.borderStrong
                        Behavior on width { NumberAnimation { duration: Tokens.durFast } } }
                }
            }

            // --- 0 Bienvenida ---
            Column { width: parent.width; spacing: 16; visible: view.step === 0
                Text { text: "Bienvenido a"; color: Tokens.accentSoft; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
                Text { text: "CinemaLove"; color: Tokens.text; font.pixelSize: 46; font.family: Tokens.fontDisplay; font.weight: Font.DemiBold }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 15; lineHeight: 1.4
                       text: "Tu catálogo de cine personal: pósters, sinopsis, reparto, tu progreso y tus colecciones, con reproducción directa de tus archivos. Vamos a elegir dónde guardar tus datos; si ya usas otra versión de CinemaLove, puedes abrir esos mismos datos." }
                AdminButton { text: "Empezar"; primary: true; onClicked: view.step = 1 }
            }

            // --- 1 Carpeta de datos ---
            Column { width: parent.width; spacing: 14; visible: view.step === 1
                Text { text: "¿Dónde están tus datos?"; color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.35
                       text: "Elige la carpeta donde está tu colección (la que contiene cinemalove.sqlite) o una carpeta vacía para empezar de cero." }
                Row { spacing: 8; width: parent.width
                    TextField { id: pathField; width: parent.width - 150; height: 44; text: view.chosen; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                        placeholderText: "Ruta de la carpeta"; placeholderTextColor: Tokens.textFaint; selectByMouse: true
                        background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: pathField.activeFocus ? Tokens.accent : Tokens.border }
                        onTextEdited: view.chosen = text }
                    AdminButton { height: 44; text: "Elegir carpeta…"; onClicked: folderDialog.open() }
                }
                Text { visible: view.chosen === ""; color: Tokens.accentSoft; font.pixelSize: 13; text: "Usar la carpeta sugerida:  " + dataFolder.suggested()
                       HoverHandler { cursorShape: Qt.PointingHandCursor }
                       TapHandler { onTapped: view.chosen = dataFolder.suggested() } }

                // Resultado de mirar la carpeta
                Rectangle {
                    visible: view.chosen !== ""
                    width: parent.width; height: resCol.height + 28; radius: Tokens.radius
                    color: view.info.locked || view.info.error || (view.info.exists && !view.info.writable) ? "#1AE5484D" : (view.info.hasCollection ? "#144CAF7D" : Tokens.surface)
                    border.color: view.info.locked || view.info.error || (view.info.exists && !view.info.writable) ? "#66E5484D" : (view.info.hasCollection ? "#664CAF7D" : Tokens.border)
                    Column { id: resCol; anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 } spacing: 6
                        Text { width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 15; font.weight: Font.DemiBold
                               color: view.info.locked || view.info.error || (view.info.exists && !view.info.writable) ? Tokens.danger : (view.info.hasCollection ? Tokens.success : Tokens.text)
                               text: view.info.locked ? "Estos datos están en uso por otra instancia"
                                     : view.info.error ? "No se puede usar esta carpeta"
                                     : (view.info.exists && !view.info.writable) ? "No hay permiso para escribir aquí"
                                     : view.info.hasCollection ? "Encontramos una colección existente"
                                     : "Se creará una colección nueva en esta carpeta" }
                        Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13; lineHeight: 1.3
                               text: view.info.locked ? "Cierra la otra aplicación de CinemaLove (esta u otra versión) y vuelve a elegir la carpeta. Solo puede haber una a la vez."
                                     : view.info.error ? view.info.error
                                     : (view.info.exists && !view.info.writable) ? "Elige otra carpeta o corrige los permisos."
                                     : view.info.hasCollection ? view.info.peliculas + " películas · " + view.info.series + " series · " + view.info.bibliotecas + " bibliotecas  (" + Number(view.info.sizeMb).toFixed(1) + " MB).\nAntes de abrirla se hará una copia de seguridad automática de cinemalove.sqlite junto a ella; tus datos originales no se modifican ni se mueven."
                                     : "Podrás agregar tus bibliotecas de películas y series después." }
                    }
                }
                Row { anchors.right: parent.right; spacing: 10
                    AdminButton { text: "Atrás"; onClicked: view.step = 0 }
                    AdminButton {
                        text: view.info.hasCollection ? "Abrir esta colección" : "Crear colección"; primary: true
                        enabled: view.chosen !== "" && !view.info.locked && !view.info.error && (!view.info.exists || view.info.writable)
                                 && (view.info.exists || view.info.writable)
                        onClicked: { if (dataFolder.apply(view.chosen)) { view.step = 2; backend.restart(dataFolder.path) } }
                    }
                }
            }

            // --- 2 Abriendo ---
            Column { width: parent.width; spacing: 14; visible: view.step === 2
                Text { text: backend.state === "error" ? view.errorTitle() : "Abriendo tu colección…"; color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                BusyIndicator { visible: backend.state !== "error"; running: visible; palette.dark: Tokens.accent }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.35
                       text: backend.state === "error" ? view.errorBody() : "Haciendo la copia de seguridad y comprobando la base de datos…" }
                Row { visible: backend.state === "error"; anchors.right: parent.right; spacing: 10
                    AdminButton { text: "Elegir otra carpeta"; onClicked: { dataFolder.forget(); backend.stop(); view.step = 1 } }
                    AdminButton { text: "Reintentar"; primary: true; onClicked: backend.restart(dataFolder.path) }
                }
            }

            // --- 3 TMDb ---
            Column { width: parent.width; spacing: 14; visible: view.step === 3
                Text { text: "Clave de TMDb"; color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.35
                       text: "CinemaLove usa TMDb (The Movie Database) para traer pósters, sinopsis y reparto. Necesitas una clave gratuita de su API; se guarda solo en tu base de datos local. Puedes hacerlo más tarde en Administración." }
                TextField { id: keyField; width: parent.width; height: 44; echoMode: TextInput.Password; placeholderText: "Pega aquí tu clave (v3)"; placeholderTextColor: Tokens.textFaint
                    color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                    background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: keyField.activeFocus ? Tokens.accent : Tokens.border } }
                Text { id: keyMsg; visible: text !== ""; width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 13 }
                Row { anchors.right: parent.right; spacing: 10
                    AdminButton { text: "Omitir por ahora"; onClicked: view.step = 4 }
                    AdminButton { text: "Probar y guardar"; primary: true; enabled: keyField.text.length > 0
                        onClicked: view.api.request("POST", "/api/v1/admin/ajustes/tmdb/probar", { apiKey: keyField.text }, (s, d, err) => {
                            if (s !== 200) { keyMsg.color = Tokens.danger; keyMsg.text = err || "La clave no es válida."; return }
                            view.api.request("PUT", "/api/v1/admin/ajustes/tmdb", { apiKey: keyField.text }, (s2, d2, e2) => {
                                if (s2 === 200) view.step = 4; else { keyMsg.color = Tokens.danger; keyMsg.text = e2 || "No se pudo guardar." } }) }) }
                }
            }

            // --- 4 Primera biblioteca ---
            Column { width: parent.width; spacing: 12; visible: view.step === 4
                Text { text: "Tu primera biblioteca"; color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.35
                       text: "Indica una carpeta con tus películas o series. CinemaLove la escaneará y buscará la información; tus archivos no se modifican." }
                Row { spacing: 8
                    Repeater { model: [ { k: "pelicula", t: "Películas" }, { k: "serie", t: "Series" } ]
                        delegate: Rectangle { width: 130; height: 40; radius: Tokens.radiusSmall
                            color: libType.value === modelData.k ? "#24E23F52" : Tokens.surface; border.color: libType.value === modelData.k ? Tokens.accent : Tokens.border
                            Text { anchors.centerIn: parent; text: modelData.t; font.pixelSize: 14; color: libType.value === modelData.k ? Tokens.accentSoft : Tokens.textMuted }
                            TapHandler { onTapped: libType.value = modelData.k }
                            HoverHandler { cursorShape: Qt.PointingHandCursor } } }
                    QtObject { id: libType; property string value: "pelicula" }
                }
                TextField { id: libName; width: parent.width; height: 42; placeholderText: "Nombre (p. ej. Películas)"; placeholderTextColor: Tokens.textFaint; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                    background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: libName.activeFocus ? Tokens.accent : Tokens.border } }
                Row { spacing: 8; width: parent.width
                    TextField { id: libPath; width: parent.width - 120; height: 42; placeholderText: "Carpeta"; placeholderTextColor: Tokens.textFaint; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                        background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: libPath.activeFocus ? Tokens.accent : Tokens.border } }
                    AdminButton { height: 42; text: "Elegir…"; onClicked: libDialog.open() } }
                Text { id: libMsg; visible: text !== ""; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13 }
                Row { anchors.right: parent.right; spacing: 10
                    AdminButton { text: "Omitir por ahora"; onClicked: view.step = 5 }
                    AdminButton { text: "Crear y escanear"; primary: true; enabled: libName.text.trim() !== "" && libPath.text.trim() !== ""
                        onClicked: view.api.request("POST", "/api/v1/admin/bibliotecas", { nombre: libName.text.trim(), tipo: libType.value, ruta: libPath.text.trim(), escanear: true }, (s, d, err) => {
                            if (s === 200 || s === 201) view.step = 5; else libMsg.text = err || "No se pudo crear la biblioteca." }) }
                }
            }

            // --- 5 Listo ---
            Column { width: parent.width; spacing: 14; visible: view.step === 5
                Text { text: "Todo listo"; color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.4
                       text: "Tu colección está abierta en  " + dataFolder.path + (backend.info.respaldo ? "\n\nSe hizo una copia de seguridad antes de abrirla:  " + backend.info.respaldo : "") }
                AdminButton { text: "Entrar a CinemaLove"; primary: true; onClicked: view.finished() }
            }
        }
    }

    FolderDialog { id: folderDialog; title: "Elegir la carpeta de datos"
        onAccepted: view.chosen = selectedFolder.toString().replace(/^file:\/\/\//, "").replace(/\//g, "\\") }
    FolderDialog { id: libDialog; title: "Elegir la carpeta de la biblioteca"
        onAccepted: libPath.text = selectedFolder.toString().replace(/^file:\/\/\//, "").replace(/\//g, "\\") }
}

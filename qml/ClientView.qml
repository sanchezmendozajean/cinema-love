import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Modo Cliente (specs/heredadas/03 §2 Camino B): conectarse a otro CinemaLove. Verificar dirección → credencial → sincronizar el catálogo.
// También las pantallas de problema: sin conexión, sesión vencida y certificado cambiado (nunca se cae a Modo Local en silencio).
Rectangle {
    id: view
    color: Tokens.bg
    property var ctl                    // ApiClient del servidor de control del Modo Cliente
    property var conn: ({ tipo: "desconectada" })
    property var mirror: ({ fase: "inactivo", hecho: 0, total: 0 })
    property var remembered: []
    signal cancel()                     // volver a elegir «mi colección en este equipo»
    signal connectedOk(string serverId)

    property string stage: "url"        // url | cred
    property var probe: null
    property string trustHash: ""
    property int credKind: 0            // 0 credenciales · 1 token
    property bool busy: false
    property string error: ""

    readonly property string connType: conn.tipo || "desconectada"
    readonly property bool problem: connType === "sin-conexion" || connType === "sesion-vencida" || connType === "certificado-cambiado"
    readonly property bool syncing: connType === "conectada"

    function reset() { stage = "url"; probe = null; trustHash = ""; error = ""; busy = false; passField.text = ""; keyField.text = "" }

    function verify() {
        if (urlField.text.trim() === "") return
        busy = true; error = ""; probe = null
        ctl.request("POST", "/control/probar", { url: urlField.text.trim() }, (st, d) => {
            busy = false
            if (st !== 200 || !d) { error = "No se pudo verificar la dirección."; return }
            probe = d
            if (d.tipo === "ok") stage = "cred"
        })
    }
    function trustAndContinue() { trustHash = probe.huella; stage = "cred" }

    function connect() {
        busy = true; error = ""
        const cred = credKind === 0 ? { tipo: "sesion", usuario: userField.text.trim(), password: passField.text } : { tipo: "apiKey", apiKey: keyField.text.trim() }
        const body = { url: probe && probe.url ? probe.url : urlField.text.trim(), credencial: cred, recordar: rememberBox.checked }
        if (trustHash !== "") body.confiarHuella = trustHash
        ctl.request("POST", "/control/conectar", body, (st, d) => {
            busy = false
            if (st !== 200 || !d) { error = "No se pudo conectar."; return }
            if (!d.ok) { error = d.mensaje || "No se pudo conectar."; return }
            if (rememberBox.checked && d.estado.info.servidorId) connectedOk(d.estado.info.servidorId)
            else connectedOk("")
        })
    }
    function act(path, body) {
        busy = true
        ctl.request("POST", path, body === undefined ? null : body, (st, d) => { busy = false; if (d && d.ok === false) error = d.mensaje || "No se pudo completar."; else error = "" })
    }

    // Identidad violeta del Modo Cliente
    Rectangle { anchors.fill: parent; gradient: Gradient {
        GradientStop { position: 0.0; color: "#1F9519AE" } GradientStop { position: 0.6; color: "#000E0D11" } } }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(640, view.width - 48); height: Math.min(view.height - 40, col.height + 64)
        radius: Tokens.radius + 6; color: Tokens.surfaceRaised; border.color: Tokens.borderStrong; clip: true

        Column {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 32 }
            spacing: 16

            // ---------- Problemas con una conexión recordada ----------
            Column { width: parent.width; spacing: 14; visible: view.problem
                Text { color: Tokens.accentSoft; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.5; text: "MODO CLIENTE" }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold
                       text: view.connType === "sin-conexion" ? "No se pudo conectar a «" + (view.conn.nombre || "") + "»"
                             : view.connType === "sesion-vencida" ? "Tu sesión venció"
                             : "El certificado de «" + (view.conn.nombre || "") + "» cambió" }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.35
                       text: view.connType === "sin-conexion" ? "Revisa que el otro equipo esté encendido y comparta su colección (" + (view.conn.url || "") + ")."
                             : view.connType === "sesion-vencida" ? "Escribe de nuevo la contraseña de «" + (view.conn.usuario || "") + "» en «" + (view.conn.nombre || "") + "»."
                             : "El servidor presenta un certificado distinto al que habías confiado. Si no cambiaste el certificado del servidor a propósito, no continúes." }
                Text { visible: view.connType === "certificado-cambiado"; width: parent.width; wrapMode: Text.WrapAnywhere; color: Tokens.textFaint; font.pixelSize: 12; font.family: "Consolas"
                       text: "Huella nueva: " + (view.conn.huellaNueva || "") }
                TextField { id: renewPass; visible: view.connType === "sesion-vencida"; width: parent.width; height: 44; echoMode: TextInput.Password; placeholderText: "Contraseña"; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                    placeholderTextColor: Tokens.textFaint; background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: renewPass.activeFocus ? Tokens.accent : Tokens.border }
                    onAccepted: view.act("/control/renovar", { password: text }) }
                Text { visible: view.error !== ""; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; text: view.error }
                Row { spacing: 10; anchors.right: parent.right
                    AdminButton { text: "Volver a mi colección"; onClicked: view.cancel() }
                    AdminButton { text: "Olvidar este servidor"; danger: true; onClicked: { view.act("/control/olvidar"); view.reset() } }
                    AdminButton { visible: view.connType === "sin-conexion"; text: "Reintentar"; primary: true; enabled: !view.busy; onClicked: view.act("/control/reintentar") }
                    AdminButton { visible: view.connType === "sesion-vencida"; text: "Renovar sesión"; primary: true; enabled: !view.busy && renewPass.text !== ""
                                  onClicked: view.act("/control/renovar", { password: renewPass.text }) }
                    AdminButton { visible: view.connType === "certificado-cambiado"; text: "Confío en el nuevo"; primary: true; enabled: !view.busy
                                  onClicked: view.act("/control/confiar", { huella: view.conn.huellaNueva }) }
                }
            }

            // ---------- Sincronizando ----------
            Column { width: parent.width; spacing: 14; visible: !view.problem && view.syncing
                Text { color: Tokens.accentSoft; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.5; text: "MODO CLIENTE" }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold
                       text: "Conectado a «" + ((view.conn.info && view.conn.info.nombre) || "") + "»" }
                BusyIndicator { running: visible; palette.dark: Tokens.accent }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14
                       text: view.mirror.fase === "error" ? "No se pudo leer el catálogo: " + (view.mirror.mensaje || "") : (view.mirror.mensaje || "Preparando el catálogo…") }
                Rectangle { visible: view.mirror.total > 0; width: parent.width; height: 6; radius: 3; color: Tokens.border
                    Rectangle { height: parent.height; radius: 3; color: Tokens.accent; width: parent.width * Math.min(1, view.mirror.hecho / Math.max(1, view.mirror.total)) } }
                Row { anchors.right: parent.right; spacing: 10; visible: view.mirror.fase === "error"
                    AdminButton { text: "Desconectar"; onClicked: { view.act("/control/desconectar"); view.cancel() } }
                    AdminButton { text: "Reintentar"; primary: true; onClicked: view.act("/control/refrescar") } }
            }

            // ---------- 1 · Dirección ----------
            Column { width: parent.width; spacing: 14; visible: !view.problem && !view.syncing && view.stage === "url"
                Text { color: Tokens.accentSoft; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.5; text: "MODO CLIENTE" }
                Text { color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold; text: "Conectarme a otro CinemaLove" }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 14; lineHeight: 1.35
                       text: "Mira el catálogo de otro equipo de tu casa que tenga activado «Compartir mi colección en la red». Escribe su dirección completa, por ejemplo http://192.168.0.115:8080." }
                // Servidores recordados
                Repeater {
                    model: view.remembered
                    delegate: Rectangle {
                        required property var modelData
                        width: col.width; height: 54; radius: Tokens.radius; color: rh.hovered ? Tokens.surfaceHover : Tokens.surface; border.color: Tokens.border
                        Column { anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter } spacing: 2
                            Text { text: modelData.nombre || modelData.url; color: Tokens.text; font.pixelSize: 15; font.weight: Font.Medium }
                            Text { text: modelData.url + "  ·  recordado"; color: Tokens.textMuted; font.pixelSize: 12 } }
                        Text { anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter } text: "Conectar ›"; color: Tokens.accentSoft; font.pixelSize: 14 }
                        HoverHandler { id: rh; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: view.act("/control/reconectar", { id: modelData.id }) }
                    }
                }
                TextField { id: urlField; width: parent.width; height: 46; placeholderText: "http://192.168.0.115:8080"; color: Tokens.text; font.pixelSize: 15; leftPadding: 14
                    placeholderTextColor: Tokens.textFaint; selectByMouse: true; inputMethodHints: Qt.ImhUrlCharactersOnly
                    background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: urlField.activeFocus ? Tokens.accent : Tokens.border }
                    onAccepted: view.verify(); onTextEdited: { view.probe = null; view.error = "" } }
                // Resultado de la verificación
                Text { visible: view.error !== ""; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; text: view.error }
                Text { visible: view.probe && (view.probe.tipo === "sin-respuesta" || view.probe.tipo === "no-cinemalove" || view.probe.tipo === "url-invalida")
                       width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13
                       text: !view.probe ? "" : view.probe.tipo === "sin-respuesta" ? "No hay respuesta en esa dirección. Revisa que el otro equipo esté encendido y comparta su colección."
                             : view.probe.tipo === "no-cinemalove" ? "Hay algo en esa dirección, pero no es un servidor de CinemaLove."
                             : "La dirección no es válida." }
                Rectangle {
                    visible: view.probe && (view.probe.tipo === "certificado-no-confiable" || view.probe.tipo === "certificado-cambiado")
                    width: parent.width; height: certCol.height + 28; radius: Tokens.radius; color: "#1AE0A63A"; border.color: "#66E0A63A"
                    Column { id: certCol; anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 } spacing: 8
                        Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.warning; font.pixelSize: 15; font.weight: Font.DemiBold
                               text: "Certificado autofirmado" }
                        Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13; lineHeight: 1.3
                               text: "Compara esta huella SHA-256 con la que muestra el otro equipo en Administración › Servidor. Si coincide, puedes confiar." }
                        Text { width: parent.width; wrapMode: Text.WrapAnywhere; color: Tokens.text; font.pixelSize: 12; font.family: "Consolas"; text: view.probe ? (view.probe.huella || "") : "" }
                        AdminButton { text: "Confío en este certificado"; primary: true; onClicked: view.trustAndContinue() }
                    }
                }
                Row { anchors.right: parent.right; spacing: 10
                    AdminButton { text: "Volver"; onClicked: view.cancel() }
                    AdminButton { text: view.busy ? "Verificando…" : "Verificar"; primary: true; enabled: !view.busy && urlField.text.trim() !== ""; onClicked: view.verify() }
                }
            }

            // ---------- 2 · Credencial ----------
            Column { width: parent.width; spacing: 14; visible: !view.problem && !view.syncing && view.stage === "cred"
                Text { color: Tokens.accentSoft; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.5; text: "MODO CLIENTE" }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 24; font.weight: Font.DemiBold
                       text: "Entrar a «" + (view.probe && view.probe.nombre ? view.probe.nombre : urlField.text) + "»" }
                // Selector segmentado
                Row { spacing: 0
                    Repeater { model: ["Credenciales", "Usar token"]
                        delegate: Rectangle {
                            required property string modelData; required property int index
                            width: 150; height: 38; color: view.credKind === index ? Tokens.accent : Tokens.surface; border.color: Tokens.borderStrong
                            radius: 8
                            Text { anchors.centerIn: parent; text: modelData; color: view.credKind === index ? "white" : Tokens.textMuted; font.pixelSize: 14; font.weight: Font.Medium }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: { view.credKind = index; view.error = "" } }
                        }
                    }
                }
                TextField { id: userField; visible: view.credKind === 0; width: parent.width; height: 44; placeholderText: "Usuario"; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                    placeholderTextColor: Tokens.textFaint; selectByMouse: true
                    background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: userField.activeFocus ? Tokens.accent : Tokens.border } }
                TextField { id: passField; visible: view.credKind === 0; width: parent.width; height: 44; echoMode: TextInput.Password; placeholderText: "Contraseña"; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                    placeholderTextColor: Tokens.textFaint
                    background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: passField.activeFocus ? Tokens.accent : Tokens.border }
                    onAccepted: if (view.credKind === 0) view.connect() }
                TextField { id: keyField; visible: view.credKind === 1; width: parent.width; height: 44; echoMode: TextInput.Password; placeholderText: "Token de acceso (API key)"; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                    placeholderTextColor: Tokens.textFaint; selectByMouse: true
                    background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: keyField.activeFocus ? Tokens.accent : Tokens.border }
                    onAccepted: if (view.credKind === 1) view.connect() }
                Text { visible: view.credKind === 1; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textFaint; font.pixelSize: 12; lineHeight: 1.3
                       text: "Con token, tu progreso, «Próximas a ver» y colecciones se guardan solo en este equipo." }
                Check { id: rememberBox; checked: true; text: "Recordar este servidor (se reconecta al abrir la app)" }
                Text { visible: view.error !== ""; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; text: view.error }
                Row { anchors.right: parent.right; spacing: 10
                    AdminButton { text: "Atrás"; onClicked: { view.stage = "url"; view.error = "" } }
                    AdminButton { text: view.busy ? "Conectando…" : "Conectar"; primary: true
                        enabled: !view.busy && (view.credKind === 0 ? (userField.text.trim() !== "" && passField.text !== "") : keyField.text.trim() !== "")
                        onClicked: view.connect() }
                }
            }
        }
    }
}

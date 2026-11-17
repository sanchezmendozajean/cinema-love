import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Servidor público (Fase 5): compartir la colección en la red local con otros CinemaLove / clientes Flutter.
// Estado y configuración, QR, usuarios y token de acceso. Todo son llamadas a /api/v1/admin/servidor|usuarios|api-key.
Flickable {
    id: panel
    property var api
    signal toast(string text)

    property var cfg: ({ protocolo: "http", puertoHttp: 8080, puertoHttps: 8443, nombre: "", mdns: true, autostart: false })
    property var st: ({ activo: false, urls: [], direcciones: [], huella: null, anunciando: false })
    property var users: []
    property string apiKey: ""
    property bool keyShown: false
    property string qrSvg: ""

    contentHeight: col.height + 40
    clip: true; boundsBehavior: Flickable.StopAtBounds

    function call(method, path, body, cb) { api.request(method, "/api/v1/admin" + path, body === undefined ? null : body, cb) }
    function applyView(d) { cfg = d.config; st = d.estado; if (st.activo && st.urls.length) loadQr(st.urls[0]); else qrSvg = "" }
    function load() {
        call("GET", "/servidor", undefined, (s, d) => { if (s === 200) applyView(d) })
        call("GET", "/usuarios", undefined, (s, d) => { if (s === 200) users = d })
        call("GET", "/api-key", undefined, (s, d) => { if (s === 200) apiKey = d.valor || "" })
    }
    function loadQr(url) { call("GET", "/servidor/qr?url=" + encodeURIComponent(url), undefined, (s, d) => { qrSvg = s === 200 ? d.svg : "" }) }
    function copy(text, what) { clip.text = text; clip.selectAll(); clip.copy(); toast(what + " copiado.") }
    TextEdit { id: clip; visible: false }

    Column {
        id: col
        width: parent.width; spacing: 22

        // --- Estado ---
        Rectangle {
            width: col.width; height: stCol.height + 40; radius: Tokens.radius; color: Tokens.surface; border.color: Tokens.border
            Row {
                anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 20; right: parent.right; rightMargin: 20 }
                spacing: 24
                Column {
                    id: stCol
                    width: parent.width - (qrSvg !== "" ? 184 : 0); spacing: 10
                    Row { spacing: 10
                        Rectangle { width: 10; height: 10; radius: 5; anchors.verticalCenter: parent.verticalCenter; color: panel.st.activo ? Tokens.success : Tokens.textFaint }
                        Text { text: panel.st.activo ? "Compartiendo tu colección" : "El servidor está detenido"; color: Tokens.text; font.pixelSize: 18; font.weight: Font.DemiBold } }
                    Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13; lineHeight: 1.3
                           text: panel.st.activo ? "Otros equipos pueden verla desde estas direcciones (solo lectura de catálogo y reproducción; sin administración)."
                                                 : "Inícialo para que otros CinemaLove de tu red puedan ver esta colección." }
                    Repeater {
                        model: panel.st.activo ? panel.st.urls : []
                        delegate: Row { spacing: 10
                            Text { text: modelData; color: Tokens.accentSoft; font.pixelSize: 15; font.family: "Consolas" }
                            Text { text: "Copiar"; color: cpHover.hovered ? Tokens.text : Tokens.textMuted; font.pixelSize: 12
                                   HoverHandler { id: cpHover; cursorShape: Qt.PointingHandCursor }
                                   TapHandler { onTapped: panel.copy(modelData, "Dirección") } } }
                    }
                    Text { visible: panel.st.activo && panel.st.protocolo === "https" && !!panel.st.huella; width: parent.width; wrapMode: Text.WrapAnywhere
                           color: Tokens.textFaint; font.pixelSize: 11; font.family: "Consolas"; text: "Huella SHA-256: " + (panel.st.huella || "") }
                    Row { spacing: 10; topPadding: 6
                        AdminButton { text: panel.st.activo ? "Detener servidor" : "Iniciar servidor"; primary: !panel.st.activo
                            onClicked: panel.call("POST", panel.st.activo ? "/servidor/detener" : "/servidor/iniciar", null, (s, d, err) => {
                                if (s === 200) { panel.applyView(d); panel.toast(panel.st.activo ? "Servidor iniciado." : "Servidor detenido.") } else panel.toast(err || "No se pudo completar.") }) }
                        AdminButton { visible: panel.cfg.protocolo === "https"; text: "Regenerar certificado…"; onClicked: certDlg.open() }
                    }
                }
                Image { visible: panel.qrSvg !== ""; width: 160; height: 160; fillMode: Image.PreserveAspectFit; sourceSize: Qt.size(320, 320)
                        source: panel.qrSvg !== "" ? "data:image/svg+xml;base64," + Qt.btoa(panel.qrSvg) : "" }
            }
        }

        // --- Configuración ---
        Rectangle {
            width: col.width; height: cfgCol.height + 40; radius: Tokens.radius; color: Tokens.surface; border.color: Tokens.border
            Column {
                id: cfgCol
                anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 20; right: parent.right; rightMargin: 20 }
                spacing: 14
                Text { text: "Configuración"; color: Tokens.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Row { spacing: 20
                    Column { spacing: 6; Text { text: "Nombre del servidor"; color: Tokens.textMuted; font.pixelSize: 12 }
                        TextField { id: nameF; width: 260; height: 40; text: panel.cfg.nombre; color: Tokens.text; font.pixelSize: 14; leftPadding: 12; maximumLength: 60
                            background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.bg; border.color: nameF.activeFocus ? Tokens.accent : Tokens.border } } }
                    Column { spacing: 6; Text { text: "Protocolo"; color: Tokens.textMuted; font.pixelSize: 12 }
                        Row { spacing: 6
                            Repeater { model: ["http", "https"]
                                delegate: Rectangle { width: 80; height: 40; radius: Tokens.radiusSmall; opacity: panel.st.activo ? 0.5 : 1
                                    color: panel.cfg.protocolo === modelData ? "#24E23F52" : Tokens.bg; border.color: panel.cfg.protocolo === modelData ? Tokens.accent : Tokens.border
                                    Text { anchors.centerIn: parent; text: modelData.toUpperCase(); color: panel.cfg.protocolo === modelData ? Tokens.accentSoft : Tokens.textMuted; font.pixelSize: 13; font.weight: Font.Medium }
                                    TapHandler { enabled: !panel.st.activo; onTapped: panel.cfg = Object.assign({}, panel.cfg, { protocolo: modelData }) } } } } }
                    Column { spacing: 6; Text { text: "Puerto"; color: Tokens.textMuted; font.pixelSize: 12 }
                        TextField { id: portF; width: 100; height: 40; enabled: !panel.st.activo; color: Tokens.text; font.pixelSize: 14; leftPadding: 12; inputMethodHints: Qt.ImhDigitsOnly
                            text: String(panel.cfg.protocolo === "https" ? panel.cfg.puertoHttps : panel.cfg.puertoHttp)
                            background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.bg; border.color: portF.activeFocus ? Tokens.accent : Tokens.border } } }
                }
                Row { spacing: 28
                    Check { id: mdnsC; checked: panel.cfg.mdns; text: "Anunciar en la red (mDNS)" }
                    Check { id: autoC; checked: panel.cfg.autostart; text: "Iniciar con la aplicación" }
                }
                AdminButton { text: "Guardar configuración"; primary: true
                    onClicked: {
                        const body = { nombre: nameF.text, mdns: mdnsC.checked, autostart: autoC.checked }
                        if (!panel.st.activo) { body.protocolo = panel.cfg.protocolo; const p = parseInt(portF.text); if (panel.cfg.protocolo === "https") body.puertoHttps = p; else body.puertoHttp = p }
                        panel.call("PUT", "/servidor/config", body, (s, d, err) => { if (s === 200) { panel.applyView(d); panel.toast("Configuración guardada.") } else panel.toast(err || "No se pudo guardar.") })
                    } }
            }
        }

        // --- Usuarios ---
        Rectangle {
            width: col.width; height: usersCol.height + 40; radius: Tokens.radius; color: Tokens.surface; border.color: Tokens.border
            Column {
                id: usersCol
                anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 20; right: parent.right; rightMargin: 20 }
                spacing: 10
                Row { spacing: 14
                    Text { text: "Usuarios"; color: Tokens.text; font.pixelSize: 16; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                    AdminButton { text: "＋  Nuevo usuario"; onClicked: newUser.open() } }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13
                       text: "Cada usuario tiene su propio progreso, Próximas a ver y colecciones al conectarse desde otro equipo." }
                Text { visible: panel.users.length === 0; color: Tokens.textFaint; font.pixelSize: 13; text: "Aún no hay usuarios." }
                Repeater {
                    model: panel.users
                    delegate: Rectangle {
                        required property var modelData
                        width: usersCol.width; height: 52; radius: Tokens.radiusSmall; color: Tokens.bg; border.color: Tokens.border
                        Column { anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter } spacing: 2
                            Text { text: modelData.nombreUsuario; color: Tokens.text; font.pixelSize: 14; font.weight: Font.Medium }
                            Text { text: modelData.sesiones + (modelData.sesiones === 1 ? " sesión activa" : " sesiones activas"); color: Tokens.textFaint; font.pixelSize: 12 } }
                        Row { anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter } spacing: 8
                            AdminButton { text: "Cambiar contraseña"; onClicked: { pwUser.target = modelData; pwUser.open() } }
                            AdminButton { text: "Eliminar…"; danger: true; onClicked: { delUser.target = modelData; delUser.open() } } }
                    }
                }
            }
        }

        // --- Token de acceso ---
        Rectangle {
            width: col.width; height: keyCol.height + 40; radius: Tokens.radius; color: Tokens.surface; border.color: Tokens.border
            Column {
                id: keyCol
                anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 20; right: parent.right; rightMargin: 20 }
                spacing: 10
                Text { text: "Token de acceso"; color: Tokens.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13
                       text: "Alternativa a usuario y contraseña para clientes que solo reproducen. Quien lo tenga puede ver tu catálogo: trátalo como una contraseña." }
                Text { color: Tokens.text; font.pixelSize: 14; font.family: "Consolas"; elide: Text.ElideRight; width: parent.width
                       text: panel.apiKey === "" ? "(aún no se ha generado)" : (panel.keyShown ? panel.apiKey : "•".repeat(Math.min(32, panel.apiKey.length))) }
                Row { spacing: 10
                    AdminButton { visible: panel.apiKey !== ""; text: panel.keyShown ? "Ocultar" : "Mostrar"; onClicked: panel.keyShown = !panel.keyShown }
                    AdminButton { visible: panel.apiKey !== ""; text: "Copiar"; onClicked: panel.copy(panel.apiKey, "Token") }
                    AdminButton { text: panel.apiKey === "" ? "Generar token" : "Regenerar…"; danger: panel.apiKey !== ""; onClicked: panel.apiKey === "" ? panel.regenKey() : keyDlg.open() } }
            }
        }
    }
    function regenKey() { call("POST", "/api-key/regenerar", null, (s, d, err) => { if (s === 200) { apiKey = d.valor; keyShown = true; toast("Token generado.") } else toast(err || "No se pudo generar.") }) }

    // --- Diálogos ---
    Modal {
        id: newUser
        heading: "Nuevo usuario"
        onOpened: { uName.text = ""; uPass.text = ""; uErr.text = ""; uName.forceActiveFocus() }
        Column { width: parent.width; spacing: 12
            TextField { id: uName; width: parent.width; height: 42; placeholderText: "Nombre de usuario"; placeholderTextColor: Tokens.textFaint; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: uName.activeFocus ? Tokens.accent : Tokens.border } }
            TextField { id: uPass; width: parent.width; height: 42; placeholderText: "Contraseña"; placeholderTextColor: Tokens.textFaint; echoMode: TextInput.Password; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: uPass.activeFocus ? Tokens.accent : Tokens.border } }
            Text { id: uErr; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; visible: text !== "" }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: newUser.close() }
                AdminButton { text: "Crear usuario"; primary: true; enabled: uName.text.trim() !== "" && uPass.text !== ""
                    onClicked: panel.call("POST", "/usuarios", { nombreUsuario: uName.text.trim(), password: uPass.text }, (s, d, err) => {
                        if (s === 201 || s === 200) { newUser.close(); panel.load(); panel.toast("Usuario creado.") } else uErr.text = err || "No se pudo crear." }) }
            } }
    }
    Modal {
        id: pwUser
        property var target: ({ id: "", nombreUsuario: "" })
        heading: "Cambiar contraseña · " + target.nombreUsuario
        onOpened: { pwF.text = ""; pwErr.text = ""; pwF.forceActiveFocus() }
        Column { width: parent.width; spacing: 12
            TextField { id: pwF; width: parent.width; height: 42; placeholderText: "Nueva contraseña"; placeholderTextColor: Tokens.textFaint; echoMode: TextInput.Password; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: pwF.activeFocus ? Tokens.accent : Tokens.border } }
            Text { id: pwErr; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; visible: text !== "" }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: pwUser.close() }
                AdminButton { text: "Guardar"; primary: true; enabled: pwF.text !== ""
                    onClicked: panel.call("PUT", "/usuarios/" + pwUser.target.id + "/password", { password: pwF.text }, (s, d, err) => {
                        if (s === 200) { pwUser.close(); panel.toast("Contraseña actualizada.") } else pwErr.text = err || "No se pudo cambiar." }) }
            } }
    }
    Modal {
        id: delUser
        property var target: ({ id: "", nombreUsuario: "" })
        heading: "Eliminar usuario"
        onOpened: { delF.text = ""; delErr.text = "" }
        Column { width: parent.width; spacing: 12
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14; lineHeight: 1.35
                   text: "Se eliminará a «" + delUser.target.nombreUsuario + "» con su progreso, Próximas a ver y colecciones, y se cerrarán sus sesiones. Esto no se puede deshacer." }
            Text { text: "Para confirmar, escribe el nombre exacto del usuario:"; color: Tokens.textMuted; font.pixelSize: 13 }
            TextField { id: delF; width: parent.width; height: 42; placeholderText: delUser.target.nombreUsuario; placeholderTextColor: Tokens.textFaint; color: Tokens.text; font.pixelSize: 14; leftPadding: 12
                background: Rectangle { radius: Tokens.radiusSmall; color: Tokens.surface; border.color: delF.activeFocus ? Tokens.danger : Tokens.border } }
            Text { id: delErr; width: parent.width; wrapMode: Text.WordWrap; color: Tokens.danger; font.pixelSize: 13; visible: text !== "" }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: delUser.close() }
                AdminButton { text: "Eliminar usuario"; danger: true; primary: true; enabled: delF.text === delUser.target.nombreUsuario
                    onClicked: panel.call("DELETE", "/usuarios/" + delUser.target.id + "?confirm=" + encodeURIComponent(delF.text), null, (s, d, err) => {
                        if (s === 200) { delUser.close(); panel.load(); panel.toast("Usuario eliminado.") } else delErr.text = err || "No se pudo eliminar." }) }
            } }
    }
    Modal {
        id: certDlg
        heading: "Regenerar certificado"
        Column { width: parent.width; spacing: 16
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14; lineHeight: 1.35
                   text: "Se creará un certificado nuevo. Los equipos que ya confiaban en el anterior tendrán que volver a confirmar la huella para conectarse." }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: certDlg.close() }
                AdminButton { text: "Regenerar"; danger: true; primary: true
                    onClicked: panel.call("POST", "/servidor/certificado/regenerar", null, (s, d, err) => { certDlg.close(); if (s === 200) { panel.applyView(d); panel.toast("Certificado regenerado.") } else panel.toast(err || "No se pudo regenerar.") }) }
            } }
    }
    Modal {
        id: keyDlg
        heading: "Regenerar token de acceso"
        Column { width: parent.width; spacing: 16
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14; lineHeight: 1.35
                   text: "El token actual dejará de funcionar de inmediato: los clientes que lo usen tendrán que configurar el nuevo." }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: keyDlg.close() }
                AdminButton { text: "Regenerar"; danger: true; primary: true; onClicked: { keyDlg.close(); panel.regenKey() } }
            } }
    }
}

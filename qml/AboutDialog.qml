import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// «Acerca de» con versiones y licencias (specs/05 §4). Qt y libmpv se enlazan dinámicamente (LGPL); el usuario puede sustituirlos.
Modal {
    id: about
    heading: "Acerca de CinemaLove"
    dialogWidth: 640
    property string mpvVersion: ""
    property string ffmpegVersion: ""

    readonly property var components: [
        { n: "Qt 6.8", l: "LGPL v3", d: "Interfaz (Qt Quick, Qt SQL, Qt Network). Enlace dinámico, sin modificar.", u: "https://www.qt.io/licensing" },
        { n: "libmpv " + (mpvVersion || ""), l: "LGPL v2.1+", d: "Reproducción de video y audio. Compilación LGPL (sin componentes GPL).", u: "https://github.com/mpv-player/mpv" },
        { n: "FFmpeg " + (ffmpegVersion || ""), l: "LGPL", d: "Decodificación, dentro de libmpv. Sin --enable-gpl ni --enable-nonfree.", u: "https://ffmpeg.org/legal.html" },
        { n: "Node.js", l: "MIT", d: "Entorno del servicio en segundo plano (catálogo, escáner, TMDb).", u: "https://github.com/nodejs/node/blob/main/LICENSE" },
        { n: "Fastify · better-sqlite3 · bcryptjs · yaml · qrcode · node-forge · bonjour-service", l: "MIT / BSD", d: "Servicio en segundo plano (API, base de datos, certificados, descubrimiento en red).", u: "" },
        { n: "The Movie Database (TMDb)", l: "Términos de TMDb", d: "Esta aplicación usa la API de TMDb pero no está avalada ni certificada por TMDb. Los pósters y datos pertenecen a sus autores.", u: "https://www.themoviedb.org/api-terms-of-use" }
    ]

    Column {
        width: parent.width; spacing: 14
        Row { spacing: 14
            Text { text: "CinemaLove"; color: Tokens.text; font.pixelSize: 26; font.family: Tokens.fontDisplay; font.weight: Font.DemiBold }
            Text { text: "versión " + Qt.application.version + " · Qt 6 + libmpv"; color: Tokens.textMuted; font.pixelSize: 13; anchors.baseline: parent.children[0].baseline } }
        Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13; lineHeight: 1.3
               text: "Tu catálogo de cine personal. Tus datos (catálogo, progreso, colecciones) son compartidos con las otras versiones de CinemaLove y no salen de tu equipo." }
        Rectangle { width: parent.width; height: 1; color: Tokens.border }
        Text { text: "COMPONENTES Y LICENCIAS"; color: Tokens.textFaint; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
        ListView {
            width: parent.width; height: Math.min(contentHeight, 300); clip: true; spacing: 12
            boundsBehavior: Flickable.StopAtBounds
            model: about.components
            delegate: Column {
                required property var modelData
                width: ListView.view.width; spacing: 2
                Row { spacing: 10
                    Text { text: modelData.n; color: Tokens.text; font.pixelSize: 14; font.weight: Font.Medium }
                    Rectangle { height: 20; width: lic.width + 14; radius: 10; color: "#24E23F52"; anchors.verticalCenter: parent.verticalCenter
                        Text { id: lic; anchors.centerIn: parent; text: modelData.l; color: Tokens.accentSoft; font.pixelSize: 11; font.weight: Font.DemiBold } } }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 12; lineHeight: 1.25; text: modelData.d }
                Text { visible: modelData.u !== ""; text: modelData.u; color: urlHover.hovered ? Tokens.accentSoft : Tokens.textFaint; font.pixelSize: 11
                       HoverHandler { id: urlHover; cursorShape: Qt.PointingHandCursor }
                       TapHandler { onTapped: Qt.openUrlExternally(modelData.u) } }
            }
        }
    }
}

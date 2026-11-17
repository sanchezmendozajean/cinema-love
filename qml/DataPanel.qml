import QtQuick
import QtQuick.Controls.Basic
import CinemaLove

// Datos (Administración): dónde está la colección, respaldos automáticos y cambio de carpeta.
Flickable {
    id: panel
    signal changeFolderRequested()
    property var backups: []
    contentHeight: col.height + 40
    clip: true; boundsBehavior: Flickable.StopAtBounds
    function load() { backups = dataFolder.backups() }
    onVisibleChanged: if (visible) load()

    Column {
        id: col
        width: parent.width; spacing: 22

        Rectangle {
            width: col.width; height: c1.height + 40; radius: Tokens.radius; color: Tokens.surface; border.color: Tokens.border
            Column { id: c1; anchors { left: parent.left; leftMargin: 20; right: parent.right; rightMargin: 20; top: parent.top; topMargin: 20 } spacing: 10
                Text { text: "Carpeta de datos"; color: Tokens.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text { width: parent.width; wrapMode: Text.WrapAnywhere; color: Tokens.accentSoft; font.pixelSize: 14; font.family: "Consolas"; text: dataFolder.path }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13; lineHeight: 1.3
                       text: "Aquí viven tu catálogo (cinemalove.sqlite), las imágenes descargadas y tus respaldos. Los mismos datos pueden abrirse con otras versiones de CinemaLove, pero no a la vez." }
                Row { spacing: 10
                    AdminButton { text: "Abrir carpeta"; onClicked: dataFolder.openInExplorer() }
                    AdminButton { text: "Usar otra carpeta…"; visible: !dataFolder.fromOverride; onClicked: confirm.open() } }
            }
        }
        Rectangle {
            width: col.width; height: c2.height + 40; radius: Tokens.radius; color: Tokens.surface; border.color: Tokens.border
            Column { id: c2; anchors { left: parent.left; leftMargin: 20; right: parent.right; rightMargin: 20; top: parent.top; topMargin: 20 } spacing: 8
                Text { text: "Copias de seguridad automáticas"; color: Tokens.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.textMuted; font.pixelSize: 13; lineHeight: 1.3
                       text: "Al abrir tu colección se hace una copia de cinemalove.sqlite si no hay ninguna o la última tiene más de 7 días; se conservan las 5 más recientes. Para restaurar una, cierra CinemaLove y reemplaza cinemalove.sqlite por esa copia (sin la parte .bak-…)." }
                Text { visible: panel.backups.length === 0; color: Tokens.textFaint; font.pixelSize: 13; text: "Aún no hay copias." }
                Repeater { model: panel.backups
                    delegate: Text { width: c2.width; elide: Text.ElideRight; color: Tokens.text; font.pixelSize: 13; font.family: "Consolas"; text: modelData } }
            }
        }
    }

    Modal {
        id: confirm
        heading: "Usar otra carpeta de datos"
        Column { width: parent.width; spacing: 16
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Tokens.text; font.pixelSize: 14; lineHeight: 1.35
                   text: "CinemaLove se cerrará de esta colección y volverá al asistente para que elijas otra carpeta. No se borra ni se mueve nada de la carpeta actual." }
            Row { anchors.right: parent.right; spacing: 10
                AdminButton { text: "Cancelar"; onClicked: confirm.close() }
                AdminButton { text: "Continuar"; primary: true; onClicked: { confirm.close(); panel.changeFolderRequested() } } }
        }
    }
}

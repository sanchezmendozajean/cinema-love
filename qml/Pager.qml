import QtQuick
import CinemaLove

// Paginador: «Mostrando 101–200 de 538» + ‹ Anterior · 1 2 3 … 6 · Siguiente ›. `page` empieza en 0.
Rectangle {
    id: pager
    property int page: 0
    property int pageCount: 1
    property int total: 0
    property int pageSize: 100
    signal go(int page)

    height: 60
    color: Tokens.bg
    visible: pageCount > 1
    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: Tokens.border }

    // Páginas a mostrar: primera, última y la actual con sus vecinas; el resto se resume con «…»
    function buildPages() {
        const out = [], n = pageCount, c = page
        const keep = new Set([0, n - 1, c - 1, c, c + 1])
        if (c <= 2) { keep.add(1); keep.add(2) }
        if (c >= n - 3) { keep.add(n - 2); keep.add(n - 3) }
        let last = -1
        for (let i = 0; i < n; i++) {
            if (!keep.has(i)) continue
            if (last >= 0 && i - last > 1) out.push(-1)
            out.push(i); last = i
        }
        return out
    }
    property var pages: buildPages()

    Text {
        anchors { left: parent.left; leftMargin: 40; verticalCenter: parent.verticalCenter }
        color: Tokens.textMuted; font.pixelSize: 13
        text: "Mostrando " + (pager.page * pager.pageSize + 1) + "–" + Math.min(pager.total, (pager.page + 1) * pager.pageSize) + " de " + pager.total
    }

    Row {
        anchors { right: parent.right; rightMargin: 40; verticalCenter: parent.verticalCenter }
        spacing: 6

        PagerButton { label: "‹  Anterior"; wide: true; enabled: pager.page > 0; onClicked: pager.go(pager.page - 1) }
        Repeater {
            model: pager.pages
            delegate: PagerButton {
                required property int modelData
                label: modelData < 0 ? "…" : String(modelData + 1)
                gap: modelData < 0
                current: modelData === pager.page
                onClicked: pager.go(modelData)
            }
        }
        PagerButton { label: "Siguiente  ›"; wide: true; enabled: pager.page < pager.pageCount - 1; onClicked: pager.go(pager.page + 1) }
    }

    component PagerButton: Rectangle {
        id: pb
        property string label
        property bool wide: false
        property bool current: false
        property bool gap: false
        signal clicked()
        width: wide ? labelText.implicitWidth + 28 : (gap ? 24 : Math.max(36, labelText.implicitWidth + 20)); height: 36
        radius: Tokens.radiusSmall + 2
        opacity: enabled ? 1 : 0.35
        color: current ? Tokens.accent : (hov.hovered && !gap && enabled ? Tokens.surfaceHover : "transparent")
        border.width: current || gap ? 0 : 1
        border.color: Tokens.border
        Behavior on color { ColorAnimation { duration: Tokens.durFast } }
        Text { id: labelText; anchors.centerIn: parent; text: pb.label; font.pixelSize: 14; font.weight: pb.current ? Font.DemiBold : Font.Medium
               color: pb.current ? "white" : (pb.gap ? Tokens.textFaint : Tokens.text) }
        HoverHandler { id: hov; cursorShape: pb.gap || !pb.enabled ? Qt.ArrowCursor : Qt.PointingHandCursor }
        TapHandler { enabled: !pb.gap && pb.enabled && !pb.current; onTapped: pb.clicked() }
        Accessible.role: Accessible.Button; Accessible.name: pb.label
    }
}

import QtQuick

// Iconos dibujados con trazos (sin fuentes externas).
// name: home | film | tv | collections | admin | info | bookmark | bookmark-filled | check | uncheck | collection-add | collection-remove
Canvas {
    id: icon
    property string name: "home"
    property color color: "white"
    property real lineWidth: 1.8
    width: 22; height: 22
    onColorChanged: requestPaint()
    onNameChanged: requestPaint()
    onWidthChanged: requestPaint()

    function rr(c, x, y, w, h, r) {   // rectángulo redondeado
        c.beginPath(); c.moveTo(x + r, y); c.lineTo(x + w - r, y); c.quadraticCurveTo(x + w, y, x + w, y + r)
        c.lineTo(x + w, y + h - r); c.quadraticCurveTo(x + w, y + h, x + w - r, y + h); c.lineTo(x + r, y + h)
        c.quadraticCurveTo(x, y + h, x, y + h - r); c.lineTo(x, y + r); c.quadraticCurveTo(x, y, x + r, y); c.closePath()
    }
    onPaint: {
        const c = getContext("2d"); c.reset()
        c.strokeStyle = color; c.fillStyle = color; c.lineWidth = lineWidth; c.lineCap = "round"; c.lineJoin = "round"
        const s = width / 22; c.scale(s, s)
        switch (name) {
        case "home":
            c.beginPath(); c.moveTo(3, 10.5); c.lineTo(11, 3.5); c.lineTo(19, 10.5); c.stroke()
            c.beginPath(); c.moveTo(5.5, 9); c.lineTo(5.5, 18.5); c.lineTo(9.5, 18.5); c.lineTo(9.5, 13); c.lineTo(12.5, 13); c.lineTo(12.5, 18.5); c.lineTo(16.5, 18.5); c.lineTo(16.5, 9); c.stroke()
            break
        case "film":
            rr(c, 3, 4, 16, 14, 2.5); c.stroke()
            c.beginPath(); c.moveTo(7.5, 4); c.lineTo(7.5, 18); c.moveTo(14.5, 4); c.lineTo(14.5, 18); c.moveTo(3, 9); c.lineTo(7.5, 9); c.moveTo(3, 13); c.lineTo(7.5, 13); c.moveTo(14.5, 9); c.lineTo(19, 9); c.moveTo(14.5, 13); c.lineTo(19, 13); c.stroke()
            break
        case "tv":
            rr(c, 3, 5, 16, 11, 2.5); c.stroke()
            c.beginPath(); c.moveTo(8, 19.5); c.lineTo(14, 19.5); c.moveTo(11, 16); c.lineTo(11, 19.5); c.stroke()
            break
        case "collections":
            rr(c, 3, 8, 12, 11, 2); c.stroke()
            c.beginPath(); c.moveTo(6.5, 5); c.lineTo(17, 5); c.quadraticCurveTo(19, 5, 19, 7); c.lineTo(19, 14.5); c.stroke()
            break
        case "admin":
            c.beginPath(); c.moveTo(4, 6.5); c.lineTo(18, 6.5); c.moveTo(4, 11); c.lineTo(18, 11); c.moveTo(4, 15.5); c.lineTo(18, 15.5); c.stroke()
            c.fillStyle = Qt.rgba(0.055, 0.051, 0.067, 1)
            for (const p of [[8, 6.5], [14, 11], [9.5, 15.5]]) { c.beginPath(); c.arc(p[0], p[1], 2.4, 0, Math.PI * 2); c.fill(); c.stroke() }
            break
        case "bookmark":
            c.beginPath(); c.moveTo(6, 3.5); c.lineTo(16, 3.5); c.quadraticCurveTo(17, 3.5, 17, 4.5); c.lineTo(17, 18.5); c.lineTo(11, 14.5); c.lineTo(5, 18.5); c.lineTo(5, 4.5)
            c.quadraticCurveTo(5, 3.5, 6, 3.5); c.closePath(); c.stroke()
            break
        case "bookmark-filled":
            c.beginPath(); c.moveTo(6, 3.5); c.lineTo(16, 3.5); c.quadraticCurveTo(17, 3.5, 17, 4.5); c.lineTo(17, 18.5); c.lineTo(11, 14.5); c.lineTo(5, 18.5); c.lineTo(5, 4.5)
            c.quadraticCurveTo(5, 3.5, 6, 3.5); c.closePath(); c.fill()
            break
        case "check":
            c.beginPath(); c.arc(11, 11, 8, 0, Math.PI * 2); c.stroke()
            c.beginPath(); c.moveTo(7.2, 11.2); c.lineTo(10, 14); c.lineTo(15, 8.2); c.stroke()
            break
        case "uncheck":
            c.beginPath(); c.arc(11, 11, 8, 0, Math.PI * 2); c.stroke()
            c.beginPath(); c.moveTo(7.8, 7.8); c.lineTo(14.2, 14.2); c.moveTo(14.2, 7.8); c.lineTo(7.8, 14.2); c.stroke()
            break
        case "collection-add":
        case "collection-remove":
            rr(c, 3, 8, 12, 11, 2); c.stroke()
            c.beginPath(); c.moveTo(6.5, 5); c.lineTo(17, 5); c.quadraticCurveTo(19, 5, 19, 7); c.lineTo(19, 10); c.stroke()
            c.beginPath(); c.moveTo(9, 13.5); c.lineTo(15, 13.5)
            if (name === "collection-add") { c.moveTo(12, 10.5); c.lineTo(12, 16.5) }
            c.stroke()
            break
        case "info":
            c.beginPath(); c.arc(11, 11, 8, 0, Math.PI * 2); c.stroke()
            c.beginPath(); c.moveTo(11, 10); c.lineTo(11, 15); c.stroke()
            c.beginPath(); c.arc(11, 7, 0.9, 0, Math.PI * 2); c.fill()
            break
        case "play":
            c.beginPath(); c.moveTo(6.5, 3.5); c.lineTo(18, 11); c.lineTo(6.5, 18.5); c.closePath(); c.fill()
            break
        case "pause":
            rr(c, 5, 3.5, 4, 15, 1); c.fill(); rr(c, 13, 3.5, 4, 15, 1); c.fill()
            break
        case "back10":
        case "fwd10": {
            const f = name === "fwd10" ? 1 : -1, cx = 11
            c.beginPath(); c.arc(cx, 12, 7.2, f > 0 ? -Math.PI * 0.35 : Math.PI * 1.35, f > 0 ? Math.PI * 1.6 : Math.PI * -0.6, f < 0); c.stroke()
            c.beginPath(); c.moveTo(cx + f * 1, 1.8); c.lineTo(cx + f * 1 + f * 3, 4.8); c.lineTo(cx + f * 1, 7.6); c.stroke()
            c.font = "bold 7.5px sans-serif"; c.textAlign = "center"; c.fillText("10", cx, 14.6)
            break }
        case "volume":
        case "volume-low":
        case "mute":
            c.beginPath(); c.moveTo(3, 8.5); c.lineTo(7, 8.5); c.lineTo(11.5, 4.5); c.lineTo(11.5, 17.5); c.lineTo(7, 13.5); c.lineTo(3, 13.5); c.closePath(); c.stroke()
            if (name === "mute") { c.beginPath(); c.moveTo(14.5, 8.5); c.lineTo(19.5, 13.5); c.moveTo(19.5, 8.5); c.lineTo(14.5, 13.5); c.stroke() }
            else { c.beginPath(); c.arc(11.5, 11, 4.2, -0.9, 0.9); c.stroke()
                   if (name === "volume") { c.beginPath(); c.arc(11.5, 11, 7.6, -0.9, 0.9); c.stroke() } }
            break
        case "audio":
            c.beginPath(); c.arc(11, 12, 7.5, Math.PI, 0); c.stroke()
            rr(c, 3, 11.5, 4, 6.5, 1.5); c.stroke(); rr(c, 15, 11.5, 4, 6.5, 1.5); c.stroke()
            break
        case "subtitles":
            rr(c, 2.5, 4.5, 17, 13, 2.5); c.stroke()
            c.beginPath(); c.moveTo(6, 10); c.lineTo(10, 10); c.moveTo(12, 10); c.lineTo(16, 10); c.moveTo(6, 13.5); c.lineTo(8.5, 13.5); c.moveTo(10.5, 13.5); c.lineTo(16, 13.5); c.stroke()
            break
        case "next":
            c.beginPath(); c.moveTo(5, 4.5); c.lineTo(14, 11); c.lineTo(5, 17.5); c.closePath(); c.fill()
            c.beginPath(); c.moveTo(17, 4.5); c.lineTo(17, 17.5); c.stroke()
            break
        case "fullscreen":
            c.beginPath(); c.moveTo(3.5, 8); c.lineTo(3.5, 3.5); c.lineTo(8, 3.5); c.moveTo(14, 3.5); c.lineTo(18.5, 3.5); c.lineTo(18.5, 8)
            c.moveTo(18.5, 14); c.lineTo(18.5, 18.5); c.lineTo(14, 18.5); c.moveTo(8, 18.5); c.lineTo(3.5, 18.5); c.lineTo(3.5, 14); c.stroke()
            break
        case "exit-fullscreen":
            c.beginPath(); c.moveTo(8, 3.5); c.lineTo(8, 8); c.lineTo(3.5, 8); c.moveTo(14, 3.5); c.lineTo(14, 8); c.lineTo(18.5, 8)
            c.moveTo(18.5, 14); c.lineTo(14, 14); c.lineTo(14, 18.5); c.moveTo(3.5, 14); c.lineTo(8, 14); c.lineTo(8, 18.5); c.stroke()
            break
        case "close":
            c.beginPath(); c.moveTo(5, 5); c.lineTo(17, 17); c.moveTo(17, 5); c.lineTo(5, 17); c.stroke()
            break
        case "back":
            c.beginPath(); c.moveTo(13.5, 4.5); c.lineTo(7, 11); c.lineTo(13.5, 17.5); c.stroke()
            break
        case "replay":
            c.beginPath(); c.arc(11, 11.5, 7, -Math.PI * 0.4, Math.PI * 1.5); c.stroke()
            c.beginPath(); c.moveTo(11, 1.8); c.lineTo(14.5, 4.6); c.lineTo(11, 7.4); c.stroke()
            break
        case "blank":
            break
        }
    }
}

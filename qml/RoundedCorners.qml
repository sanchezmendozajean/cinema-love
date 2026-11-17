import QtQuick
import QtQuick.Shapes
import CinemaLove

// Recorta visualmente las esquinas de lo que haya debajo (una imagen cuadrada) pintando las cuatro «esquinas inversas»
// con el color del fondo. Barato: sin capas ni sombreadores (importa en grillas de cientos de tarjetas).
Shape {
    id: s
    property real radius: Tokens.radius
    property color color: Tokens.bg
    preferredRendererType: Shape.CurveRenderer
    enabled: false   // no intercepta eventos

    ShapePath { // arriba izquierda
        fillColor: s.color; strokeColor: "transparent"; strokeWidth: -1
        startX: 0; startY: 0
        PathLine { x: s.radius; y: 0 }
        PathArc { x: 0; y: s.radius; radiusX: s.radius; radiusY: s.radius; direction: PathArc.Counterclockwise }
        PathLine { x: 0; y: 0 }
    }
    ShapePath { // arriba derecha
        fillColor: s.color; strokeColor: "transparent"; strokeWidth: -1
        startX: s.width; startY: 0
        PathLine { x: s.width - s.radius; y: 0 }
        PathArc { x: s.width; y: s.radius; radiusX: s.radius; radiusY: s.radius; direction: PathArc.Clockwise }
        PathLine { x: s.width; y: 0 }
    }
    ShapePath { // abajo derecha
        fillColor: s.color; strokeColor: "transparent"; strokeWidth: -1
        startX: s.width; startY: s.height
        PathLine { x: s.width; y: s.height - s.radius }
        PathArc { x: s.width - s.radius; y: s.height; radiusX: s.radius; radiusY: s.radius; direction: PathArc.Clockwise }
        PathLine { x: s.width; y: s.height }
    }
    ShapePath { // abajo izquierda
        fillColor: s.color; strokeColor: "transparent"; strokeWidth: -1
        startX: 0; startY: s.height
        PathLine { x: s.radius; y: s.height }
        PathArc { x: 0; y: s.height - s.radius; radiusX: s.radius; radiusY: s.radius; direction: PathArc.Clockwise }
        PathLine { x: 0; y: s.height }
    }
}

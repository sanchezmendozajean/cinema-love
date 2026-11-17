import QtQuick

// Desplazamiento con la rueda del ratón / panel táctil para un Flickable (o GridView/ListView) vertical.
// Cada muesca recorre `notchPixels` (50 % más que el comportamiento por defecto de Qt) y se anima para que se sienta suave.
// Se declara como HERMANO posterior del Flickable:   Flickable { id: f … }   SmoothWheel { flickable: f }
Item {
    id: root
    required property Flickable flickable
    property real notchPixels: spikeArgs.notchPixels > 0 ? spikeArgs.notchPixels : 108   // medido: Qt avanza 72 px por muesca; +50 % = 108
    property real touchpadScale: 1.5                                                        // el panel táctil entrega píxeles exactos
    property int duration: 170
    anchors.fill: flickable
    visible: flickable.visible          // si el contenedor está oculto no debe capturar la rueda
    enabled: !spikeArgs.nativeScroll

    function limits() {
        const f = flickable
        const minY = f.originY - f.topMargin
        const maxY = Math.max(minY, f.originY + f.contentHeight + f.bottomMargin - f.height)
        return [minY, maxY]
    }
    function clamp(v) { const l = limits(); return Math.max(l[0], Math.min(l[1], v)) }

    WheelHandler {
        id: handler
        orientation: Qt.Vertical
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (ev) => {
            const f = root.flickable
            if (f.contentHeight <= f.height) { ev.accepted = false; return }
            if (ev.pixelDelta.y !== 0) {            // panel táctil de precisión: movimiento directo
                anim.stop()
                f.contentY = root.clamp(f.contentY - ev.pixelDelta.y * root.touchpadScale)
                return
            }
            const delta = ev.angleDelta.y / 120 * root.notchPixels
            const base = anim.running ? anim.to : f.contentY       // acumula muescas seguidas
            anim.stop()
            anim.from = f.contentY
            anim.to = root.clamp(base - delta)
            anim.start()
        }
    }
    NumberAnimation { id: anim; target: root.flickable; property: "contentY"; duration: root.duration; easing.type: Easing.OutCubic }
}

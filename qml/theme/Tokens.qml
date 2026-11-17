pragma Singleton
import QtQuick

// Tokens de design/10-sistema-de-diseno.md («sala oscura»). Único lugar donde viven los colores.
QtObject {
    readonly property color bg: "#0E0D11"
    readonly property color surface: "#19181E"
    readonly property color surfaceRaised: "#221F28"
    readonly property color surfaceHover: "#2A2631"
    readonly property color border: "#2E2A35"
    readonly property color borderStrong: "#3D3846"
    readonly property color text: "#F5F3F7"
    readonly property color textMuted: "#A7A2B0"
    readonly property color textFaint: "#6E6878"
    readonly property color accent: "#E23F52"
    readonly property color accentSoft: "#F2707F"
    readonly property color danger: "#E5484D"
    readonly property color success: "#4CAF7D"
    readonly property color warning: "#E0A63A"
    // Tipografías: Fraunces (títulos) e Inter (interfaz) si están en third_party/fonts (se cargan en main.cpp); si no, Georgia / la del sistema.
    readonly property string fontDisplay: Qt.fontFamilies().indexOf("Fraunces") >= 0 ? "Fraunces" : "Georgia"
    readonly property int radius: 10
    readonly property int radiusSmall: 6
    readonly property int durFast: 140
}

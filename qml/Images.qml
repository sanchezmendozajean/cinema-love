pragma Singleton
import QtQuick

// Convierte una URL de imagen de TMDb en la fuente del proveedor `image://cl/` (caché compartida `images/` vía backend).
QtObject {
    function src(url) { return url ? "image://cl/" + encodeURIComponent(url) : "" }
}

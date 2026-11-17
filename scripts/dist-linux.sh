#!/usr/bin/env bash
# Empaquetado Linux x86_64 (SIN PROBAR: escrito sin acceso a una máquina Linux; revisar en el primer uso).
# Autocontenido: Qt, libmpv, el Node incluido y el backend propio. No descarga nada. Requisitos previos:
#   - third_party/node/node          (Node para Linux x86_64, con su checksum anotado en third_party/README.md)
#   - libmpv.so.2 y sus dependencias (LGPL) en third_party/libmpv/linux/ (o libmpv-dev del sistema para compilar)
#   - Qt 6.8 para Linux (QT_ROOT) y CMake/Ninja
# Uso: QT_ROOT=/opt/Qt/6.8.3/gcc_64 ./scripts/dist-linux.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${QT_ROOT:?Define QT_ROOT (p. ej. /opt/Qt/6.8.3/gcc_64)}"
node="$root/third_party/node/node"
[ -x "$node" ] || { echo "Falta $node (Node para Linux). Colócalo en third_party/node/ y anota su checksum." >&2; exit 1; }

echo "== Backend"
"$node" "$root/backend/scripts/build.mjs"
[ -f "$root/backend/dist/main.cjs" ] || { echo "No se generó backend/dist/main.cjs" >&2; exit 1; }

echo "== Compilación (Release)"
cmake -S "$root" -B "$root/build/linux" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT"
cmake --build "$root/build/linux"

dest="$root/dist/CinemaLove-linux"
rm -rf "$dest"; mkdir -p "$dest/backend" "$dest/licencias" "$dest/lib"
cp "$root/build/linux/cinemalove" "$dest/"

echo "== Qt (qt_deploy_runtime_deps / copia de bibliotecas, plugins y QML)"
# Copia las bibliotecas de Qt que enlaza el ejecutable y los plugins imprescindibles.
ldd "$dest/cinemalove" | awk '/=> \// {print $3}' | grep -E "$QT_ROOT|libmpv" | while read -r lib; do cp -n "$lib" "$dest/lib/"; done || true
for d in platforms imageformats sqldrivers tls networkinformation xcbglintegrations egldeviceintegrations; do
  [ -d "$QT_ROOT/plugins/$d" ] && { mkdir -p "$dest/plugins"; cp -r "$QT_ROOT/plugins/$d" "$dest/plugins/"; }
done
for d in QtQuick QtQml Qt QtCore QtQuick.2; do
  [ -d "$QT_ROOT/qml/$d" ] && { mkdir -p "$dest/qml"; cp -r "$QT_ROOT/qml/$d" "$dest/qml/"; }
done
[ -d "$root/third_party/libmpv/linux" ] && cp -n "$root/third_party/libmpv/linux/"*.so* "$dest/lib/" 2>/dev/null || true

echo "== Backend y Node"
cp "$node" "$dest/backend/node"
cp "$root/backend/dist/main.cjs" "$dest/backend/"
cp -r "$root/backend/node_modules" "$dest/backend/"
[ -d "$root/third_party/fonts" ] && { mkdir -p "$dest/fonts"; cp "$root"/third_party/fonts/*.ttf "$root"/third_party/fonts/*.otf "$dest/fonts/" 2>/dev/null || true; }

echo "== Lanzador"
cat > "$dest/cinemalove.sh" <<'LAUNCH'
#!/usr/bin/env bash
here="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$here/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$here/plugins"
export QML_IMPORT_PATH="$here/qml"
exec "$here/cinemalove" "$@"
LAUNCH
chmod +x "$dest/cinemalove.sh"
cp "$root/third_party/README.md" "$dest/licencias/COMPONENTES.md"

( cd "$root/dist" && tar -czf CinemaLove-linux-0.1.0.tar.gz CinemaLove-linux )
echo "Listo: $dest y dist/CinemaLove-linux-0.1.0.tar.gz (probar en una máquina limpia)."

# Empaqueta una distribución portable para Windows: dist\CinemaLove-win\ y dist\CinemaLove-win-<versión>.zip
# Autocontenida: Qt, libmpv, el Node incluido y el backend propio. No descarga nada. Requiere haber compilado antes
# (scripts\dev.ps1 o cmake --build --preset win-release) y el backend (scripts\build-backend.ps1).
param([switch]$NoZip)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$rel = Join-Path $root 'build\win\Release'
$exe = Join-Path $rel 'cinemalove.exe'
$bundle = Join-Path $root 'backend\dist\main.cjs'
$node = Join-Path $root 'third_party\node\node.exe'
foreach ($f in @($exe, $bundle, $node, (Join-Path $rel 'libmpv-2.dll'))) { if (-not (Test-Path $f)) { throw "Falta $f. Compila primero." } }

$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(CinemaLove VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$out = Join-Path $root 'dist'
$dest = Join-Path $out 'CinemaLove-win'
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item -ItemType Directory -Force $dest | Out-Null

# 1) Aplicación + Qt + libmpv (lo que dejó windeployqt junto al .exe), sin registros ni configuración de desarrollo
robocopy $rel $dest /E /NFL /NDL /NJH /NJS /NP /XF *.log *.pdb *.ilk *.exp *.lib config.yaml window.ini | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy falló ($LASTEXITCODE)" }

# 2) Backend propio: bundle + Node incluido + el único módulo nativo (better-sqlite3, solo Windows x64)
$be = Join-Path $dest 'backend'
New-Item -ItemType Directory -Force $be | Out-Null
Copy-Item $bundle (Join-Path $be 'main.cjs')
Copy-Item $node (Join-Path $be 'node.exe')
$sq = Join-Path $root 'backend\node_modules\better-sqlite3'
$sqDest = Join-Path $be 'node_modules\better-sqlite3'
New-Item -ItemType Directory -Force (Join-Path $sqDest 'prebuilds') | Out-Null
Copy-Item (Join-Path $sq 'package.json'), (Join-Path $sq 'LICENSE') $sqDest
Copy-Item (Join-Path $sq 'lib') $sqDest -Recurse
Copy-Item (Join-Path $sq 'prebuilds\win32-x64.node') (Join-Path $sqDest 'prebuilds')

# 2b) Tipografías opcionales (third_party/fonts/*.ttf|otf, OFL)
$fonts = Get-ChildItem (Join-Path $root 'third_partyonts') -Include *.ttf,*.otf -Recurse -ErrorAction SilentlyContinue
if ($fonts) { $fd = Join-Path $dest 'fonts'; New-Item -ItemType Directory -Force $fd | Out-Null; $fonts | Copy-Item -Destination $fd }

# 3) Licencias y avisos
$lic = Join-Path $dest 'licencias'
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $root 'third_party\README.md') (Join-Path $lic 'COMPONENTES.md')
Set-Content (Join-Path $lic 'LEEME.txt') -Encoding utf8 -Value @"
CinemaLove $version (Qt 6 + libmpv)

Qt 6 se enlaza dinamicamente (LGPL v3): las DLL de Qt de esta carpeta pueden sustituirse por otras compatibles.
libmpv (libmpv-2.dll) es una compilacion LGPL v2.1+; FFmpeg va incluido en ella (LGPL, sin componentes GPL).
Node.js (backend\node.exe) y las dependencias del backend son MIT/BSD.
Esta aplicacion usa la API de TMDb pero no esta avalada ni certificada por TMDb.
Consulta "Acerca de" (icono i, arriba a la derecha) y COMPONENTES.md para versiones y procedencia.
"@

Set-Content (Join-Path $dest 'LEEME.txt') -Encoding utf8 -Value @"
CinemaLove $version - version portable para Windows

1) Ejecuta cinemalove.exe. La primera vez, el asistente te pide la carpeta de tus datos.
2) Antes de abrir una coleccion existente se hace una copia de seguridad automatica de cinemalove.sqlite.
3) Solo puede haber una version de CinemaLove abierta sobre la misma carpeta de datos.
No necesita instalacion ni conexion a internet (salvo para descargar datos y posters de TMDb).
"@

$mb = [math]::Round(((Get-ChildItem $dest -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB)
Write-Host "Distribucion lista en $dest ($mb MB)"
if (-not $NoZip) {
    $zip = Join-Path $out "CinemaLove-win-$version.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path $dest -DestinationPath $zip -CompressionLevel Optimal
    Write-Host ("ZIP: {0} ({1} MB)" -f $zip, [math]::Round((Get-Item $zip).Length / 1MB))
}

exit 0   # robocopy devuelve 1 cuando copia archivos; no es un error

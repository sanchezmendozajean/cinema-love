# Compila el backend propio (backend/src → backend/dist/main.cjs) con el Node y el esbuild incluidos en la carpeta. Sin internet.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$node = Join-Path $root 'third_party\node\node.exe'
if (-not (Test-Path $node)) { throw "Falta $node (el Node incluido en el proyecto)." }
Push-Location (Join-Path $root 'backend')
try { & $node scripts\build.mjs; if ($LASTEXITCODE) { throw 'Falló la compilación del backend.' } }
finally { Pop-Location }

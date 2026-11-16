# Equivalente de `npm run dev`: configura (si hace falta), compila y lanza CinemaLove con la copia de datos de .dev-data/.
# Uso: .\scripts\dev.ps1 [-DebugBuild] [-Configure] [argumentos de la app...]   p. ej.  .\scripts\dev.ps1 --search dalmatas
param(
    [switch]$DebugBuild,
    [switch]$Configure,
    [Parameter(ValueFromRemainingArguments = $true)] [string[]]$AppArgs
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $env:QT_ROOT) { $env:QT_ROOT = 'D:/Qt/6.8.3/msvc2022_64' }

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path $cmake)) { throw "No se encuentra cmake. Instala CMake o ajusta la ruta en scripts/dev.ps1." }

$config = if ($DebugBuild) { 'Debug' } else { 'Release' }
$preset = "win-$($config.ToLower())"

Push-Location $root
try {
    if ($Configure -or -not (Test-Path "$root\build\win\CMakeCache.txt")) {
        & $cmake --preset win
        if ($LASTEXITCODE) { throw 'Falló la configuración de CMake.' }
    }
    & $cmake --build --preset $preset
    if ($LASTEXITCODE) { throw 'Falló la compilación.' }

    if (-not (Test-Path "$root\.dev-data\cinemalove.sqlite")) {
        Write-Warning 'Falta .dev-data\cinemalove.sqlite: copia ahí una copia de la base (nunca la real).'
    }
    $env:PATH = "$env:QT_ROOT/bin;$env:PATH"
    & "$root\build\win\$config\cinemalove.exe" @AppArgs
}
finally { Pop-Location }

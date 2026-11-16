# Descarga libmpv LGPL (build para Windows x64 de zhongfly/mpv-winbuild) a third_party/libmpv y verifica el checksum.
# Es un build LGPL: mpv con -Dgpl=false y ffmpeg sin --enable-gpl ni --enable-nonfree (comprobable con las propiedades
# «mpv-configuration» y «ffmpeg-configuration» de libmpv, que el spike vuelca en las métricas).
$ErrorActionPreference = 'Stop'
$tag = '2026-11-15-3d73d07e6c'
$file = 'mpv-dev-lgpl-x86_64-20261115-git-3d73d07e6c.7z'
$sha256 = '4015f541cab7e85fef8b713beeee7ea5cafd9e90929663a820deefb4f1916137'
$dest = Join-Path $PSScriptRoot '..\third_party\libmpv'
$tmp = Join-Path $env:TEMP $file
Invoke-WebRequest "https://github.com/zhongfly/mpv-winbuild/releases/download/$tag/$file" -OutFile $tmp
if ((Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower() -ne $sha256) { throw 'Checksum de libmpv incorrecto' }
$7z = 'C:\Program Files\7-Zip\7z.exe'
New-Item -ItemType Directory -Force $dest | Out-Null
& $7z x -y "-o$dest" $tmp | Out-Null
Write-Host "libmpv (LGPL) listo en $dest"

# Descarga libmpv (build para Windows x64 de shinchiro/mpv-winbuild-cmake) a third_party/libmpv y verifica el checksum.
# ATENCIÓN: ese build es GPL. Sirve para desarrollo y el spike; para distribuir hay que usar un build LGPL (specs/05).
$ErrorActionPreference = 'Stop'
$tag = '20261111'
$file = 'mpv-dev-x86_64-20261111-git-bcde6b93cc.7z'
$sha256 = '273c079b11ea5575822f972756f4c9a71149a5c10130353abeb8586738938452'
$dest = Join-Path $PSScriptRoot '..\third_party\libmpv'
$tmp = Join-Path $env:TEMP $file
Invoke-WebRequest "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/$tag/$file" -OutFile $tmp
if ((Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower() -ne $sha256) { throw 'Checksum de libmpv incorrecto' }
$7z = 'C:\Program Files\7-Zip\7z.exe'
New-Item -ItemType Directory -Force $dest | Out-Null
& $7z x -y "-o$dest" $tmp | Out-Null
Write-Host "libmpv listo en $dest"

param([Parameter(Mandatory=$true)][string]$GlewInclude,[string]$Zig='zig')
$ErrorActionPreference='Stop'
$src=Join-Path $PSScriptRoot 'src'
$installer=Join-Path $PSScriptRoot 'installer'
$out=(New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot 'dist')).FullName
$headers=(Resolve-Path -LiteralPath $GlewInclude).Path
if(!(Test-Path -LiteralPath (Join-Path $headers 'GL/glew.h'))){throw 'GlewInclude must contain GL/glew.h'}
$dll=Join-Path $out 'libvgmstream.dll'
& $Zig c++ -target x86-windows-gnu -std=c++17 -O2 -s "-I$headers" -DGEO_NATIVE_CAPTURE -shared (Join-Path $src 'geometry.cpp') (Join-Path $src 'libvgmstream_proxy.def') -o $dll -lopengl32 -lgdi32 -luser32 -lcomctl32
if($LASTEXITCODE){throw 'Renderer build failed.'}
$hash=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
$package=Join-Path $out 'Paquete.cs'
[IO.File]::WriteAllText($package,'internal static class Paquete { internal const string Firma="'+$hash+'"; }')
$payload=Join-Path $out 'mod.gz'
$file=[IO.File]::Create($payload)
try {
    $zip=[IO.Compression.GZipStream]::new($file,[IO.Compression.CompressionMode]::Compress)
    try {
        $inputDll=[IO.File]::OpenRead($dll)
        try{$inputDll.CopyTo($zip)}finally{$inputDll.Dispose()}
    }finally{$zip.Dispose()}
}finally{$file.Dispose()}
$exe=Join-Path $out 'FF8-Reborn-Setup.exe'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework/v4.0.30319/csc.exe'
$sources='Instalador.cs','Portada.cs'|ForEach-Object{Join-Path $installer $_}
$atlas=Join-Path $installer 'Atlas.jpg'
& $compiler /nologo /target:winexe /platform:anycpu /optimize+ /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll "/resource:$payload,mod.gz" "/resource:$atlas,atlas.jpg" "/out:$exe" @sources $package
if($LASTEXITCODE){throw 'Installer build failed.'}
& (Join-Path $installer 'package-deck.ps1') -Renderer $dll -Output (Join-Path $out 'FF8-Reborn-Deck.pyz')
Write-Output $exe

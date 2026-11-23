param([Parameter(Mandatory)][string]$Renderer,[Parameter(Mandatory)][string]$Output,[string]$Zig='zig')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
$file=[IO.File]::Create($Output)
try {
    $package=[IO.Compression.ZipArchive]::new($file,[IO.Compression.ZipArchiveMode]::Create)
    try {
        $files=@{'__main__.py'=(Join-Path $PSScriptRoot 'deck.py'); 'libvgmstream.dll'=$Renderer}
        foreach($asset in 'deck-theme.css','FF8Menu.ttf'){
            $files[$asset]=Join-Path $PSScriptRoot $asset
        }
        foreach($name in $files.Keys){
            $stream=$package.CreateEntry($name).Open()
            try {
                $source=[IO.File]::OpenRead($files[$name])
                try {$source.CopyTo($stream)} finally {$source.Dispose()}
            } finally {$stream.Dispose()}
        }
        $stream=$package.CreateEntry('renderer.sha256').Open()
        try {
            $bytes=[Text.Encoding]::ASCII.GetBytes((Get-FileHash -LiteralPath $Renderer).Hash)
            $stream.Write($bytes,0,$bytes.Length)
        } finally {$stream.Dispose()}
    } finally {$package.Dispose()}
} finally {$file.Dispose()}
$launcher=Join-Path (Split-Path $Output) 'FF8-Reborn-Deck-Setup'
& $Zig cc -target x86_64-linux-musl -static -Os -s (Join-Path $PSScriptRoot 'deck-launcher.c') -o $launcher
if($LASTEXITCODE){throw 'Steam Deck launcher build failed.'}
$file=[IO.File]::Open($launcher,[IO.FileMode]::Append)
try {
    $source=[IO.File]::OpenRead($Output)
    try {$source.CopyTo($file)} finally {$source.Dispose()}
} finally {$file.Dispose()}

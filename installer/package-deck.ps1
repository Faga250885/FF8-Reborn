param([Parameter(Mandatory)][string]$Renderer,[Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
$file=[IO.File]::Create($Output)
try {
    $package=[IO.Compression.ZipArchive]::new($file,[IO.Compression.ZipArchiveMode]::Create)
    try {
        $files=@{'__main__.py'=(Join-Path $PSScriptRoot 'deck.py'); 'libvgmstream.dll'=$Renderer}
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

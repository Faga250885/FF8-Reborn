# Produce release assets and feeds locally. Publish assets before advertising them.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$out=Join-Path $root 'dist/updates'
[IO.Directory]::CreateDirectory($out) | Out-Null
$versions=[IO.File]::ReadAllText((Join-Path $root 'src/update_version.h'))
$pattern='channel\[\]\s*=\s*"(?<channel>windows|deck)"\s*,\s*tag\[\]\s*=\s*"(?<tag>[^"]+)"\s*;\s*constexpr unsigned build\s*=\s*(?<build>[0-9]+)\s*;'
$rows=[regex]::Matches($versions,$pattern)
if($rows.Count -ne 2){throw 'Expected a Windows and a Steam Deck release in update_version.h'}
$seen=@{}
foreach($row in $rows){
    $channel=$row.Groups['channel'].Value
    $tag=$row.Groups['tag'].Value
    $build=[uint32]$row.Groups['build'].Value
    if($seen.ContainsKey($channel)){throw "Duplicate release channel $channel"}
    $seen[$channel]=$true
    $renderer=if($channel -eq 'deck'){'dist/deck/libvgmstream.dll'}else{'dist/libvgmstream.dll'}
    $asset=Join-Path $out "FF8-Reborn-$channel.dll"
    Copy-Item -LiteralPath (Join-Path $root $renderer) -Destination $asset -Force
    $hash=(Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()
    $feed=@('FF8-Reborn-feed-1',$channel,$build,$tag,$hash)
    [IO.File]::WriteAllText((Join-Path $out "$channel.txt"),($feed -join "`n")+"`n",[Text.Encoding]::ASCII)
}

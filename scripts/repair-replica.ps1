# Run on the origin machine when a peer returns after an outage. Only for a READY video.
param([Parameter(Mandatory=$true)][string]$Storage,[Parameter(Mandatory=$true)][string]$Id,[Parameter(Mandatory=$true)][string]$Peer,[Parameter(Mandatory=$true)][string]$Token)
$ErrorActionPreference='Stop'
if ($Id -notmatch '^[A-Za-z0-9_-]{1,64}$') {throw 'Invalid ID'}
$video=Join-Path $Storage $Id
if ((Get-Content (Join-Path $video 'status.txt') -Raw).Trim() -ne 'ready'){throw 'Source video not ready'}
$dash=Join-Path $video 'dash'
$manifest=Get-Item (Join-Path $dash 'manifest.mpd')
$files=@(Get-ChildItem $dash -File | Where-Object {$_.Extension -in '.m4s','.mp4'}) + @($manifest)
foreach($file in $files){
    curl.exe --fail --silent --show-error -X PUT -H "X-Tecflix-Token: $Token" -H 'Content-Type: application/octet-stream' --data-binary "@$($file.FullName)" "$($Peer.TrimEnd('/'))/internal/replicate/$Id/$($file.Name)"
    if($LASTEXITCODE){throw "Replication stopped: $($file.Name)"}
}
Write-Host 'Replication repaired. Check peer /status and /manifest.mpd.'

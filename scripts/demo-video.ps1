param([string]$BaseUrl='http://localhost:8083',[string]$Id='demo1')
$ErrorActionPreference='Stop'
$file=Join-Path ([IO.Path]::GetTempPath()) "tecflix-$([guid]::NewGuid()).mp4"
try {
    ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=640x360:rate=25' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 16 -c:v libx264 -threads 2 -pix_fmt yuv420p -c:a aac $file
    if ($LASTEXITCODE) {throw 'FFmpeg failed'}
    curl.exe --fail --silent --show-error -X POST -H 'Content-Type: video/mp4' --data-binary "@$file" "$BaseUrl/videos/$Id/upload"
    if ($LASTEXITCODE) {throw 'Upload failed (use a new Id if it already exists)'}
    for ($i=0;$i -lt 120;$i++) {
        $status=(Invoke-RestMethod "$BaseUrl/videos/$Id/status").status
        if ($status -eq 'ready') {Write-Host "Ready: $BaseUrl/videos/$Id/manifest.mpd"; return}
        if ($status -eq 'failed') {throw 'Transcoding failed; inspect ffmpeg.log'}
        Start-Sleep -Seconds 2
    }
    throw 'Transcoding timeout'
} finally {Remove-Item $file -ErrorAction SilentlyContinue}

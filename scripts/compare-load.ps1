param([string]$Single='http://localhost:8083',[string]$Dual='http://localhost:8080',[string]$Video='demo1',[int[]]$Users=@(1,10,25,50,100,200),[string]$Duration='60s')
$ErrorActionPreference='Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$dir=Join-Path 'load-results' (Get-Date -Format 'yyyyMMdd-HHmmss')
New-Item -ItemType Directory -Force $dir | Out-Null
$rows=@()
foreach ($target in @(@{Name='single';Url=$Single},@{Name='dual';Url=$Dual})) {
    foreach ($n in $Users) {
        $summary=Join-Path $dir "$($target.Name)-$n.json"
        k6 run -e "BASE_URL=$($target.Url)" -e "VIDEO_ID=$Video" -e "FIXED_VUS=$n" -e "DURATION=$Duration" --summary-export $summary load-tests/streaming.js
        $exit=$LASTEXITCODE
        if (!(Test-Path $summary)) {throw "k6 did not produce $summary (exit $exit)"}
        $m=(Get-Content $summary -Raw | ConvertFrom-Json).metrics
        $rows += [pscustomobject]@{Mode=$target.Name;Users=$n;AvgMs=$m.http_req_duration.avg;P95Ms=$m.http_req_duration.'p(95)';SegmentP95Ms=$m.segment_latency.'p(95)';ErrorRate=$m.http_req_failed.value;RequestsPerSec=$m.http_reqs.rate;ExitCode=$exit}
        Start-Sleep -Seconds 5
    }
}
$rows | Export-Csv (Join-Path $dir 'comparison.csv') -NoTypeInformation
$rows | Format-Table

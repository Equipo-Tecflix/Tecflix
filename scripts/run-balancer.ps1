
param(
    [Parameter(Mandatory = $true)]
    [string]$NginxExe,

    [string]$NodeA = '127.0.0.1:8083',
    [string]$NodeB = '127.0.0.1:8084',
    [int]$Port = 8080,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Instance = 'tecflix'
)

$ErrorActionPreference = 'Stop'

foreach ($node in @($NodeA, $NodeB)) {
    if ($node -notmatch '^[A-Za-z0-9.-]+:[0-9]{1,5}$') {
        throw 'Use host:port'
    }
}

$exe = (Resolve-Path $NginxExe).Path
$prefix = Join-Path (Split-Path $exe) $Instance

New-Item -ItemType Directory -Force `
    (Join-Path $prefix 'logs') | Out-Null

New-Item -ItemType Directory -Force `
    (Join-Path $prefix 'temp') | Out-Null

$config = @"
worker_processes 1;

pid logs/nginx.pid;

events {
    worker_connections 1024;
}

http {
    upstream streaming {
        least_conn;
        server $NodeA max_fails=1 fail_timeout=5s;
        server $NodeB max_fails=1 fail_timeout=5s;
        keepalive 32;
    }

    server {
        listen $Port;

        client_max_body_size 2g;

        location /internal/ {
            return 403;
        }

        location / {
            proxy_pass http://streaming;
            proxy_http_version 1.1;
            proxy_set_header Connection "";

            proxy_next_upstream error timeout http_502 http_503 http_504 http_404;
            proxy_next_upstream_tries 2;

            proxy_connect_timeout 2s;
            proxy_read_timeout 300s;
            proxy_request_buffering off;

            add_header X-Tecflix-Balancer "$Instance" always;
        }
    }
}
"@

$config | Set-Content -Encoding ascii `
    (Join-Path $prefix 'nginx.conf')

$nginxPrefix = $prefix.Replace('\', '/') + '/'

& $exe -p "$nginxPrefix" -c nginx.conf -t

if ($LASTEXITCODE) {
    throw 'Invalid nginx config'
}

& $exe -p "$nginxPrefix" -c nginx.conf

if ($LASTEXITCODE) {
    throw 'nginx failed'
}

Write-Host "Balancer: $Instance"
Write-Host "Port: $Port"
Write-Host "Node A: $NodeA"
Write-Host "Node B: $NodeB"
Write-Host "Stop with:"
Write-Host "& '$exe' -p '$nginxPrefix' -c nginx.conf -s quit"

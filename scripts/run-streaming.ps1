
param(
    [int]$Port = 8083,
    [string]$Node = 'node-1',
    [string]$Storage = '',
    [string]$Peers = '',
    [string]$Token = '',
    [string]$MetadataUrl = '',
    [int]$Jobs = 2,
    [string]$OtlpEndpoint = ''
)

$ErrorActionPreference = 'Stop'

Set-Location (Join-Path $PSScriptRoot '..')

if (!$Storage) {
    $Storage = "storage-$Node"
}

if ($Peers -and !$Token) {
    throw 'Provide a shared replication token'
}

$env:TECFLIX_PORT = "$Port"
$env:TECFLIX_NODE_ID = $Node
$env:TECFLIX_STORAGE_DIR = $Storage
$env:TECFLIX_PEERS = $Peers
$env:TECFLIX_INTERNAL_TOKEN = $Token
$env:TECFLIX_METADATA_URL = $MetadataUrl
$env:TECFLIX_MAX_JOBS = "$Jobs"
$env:TECFLIX_OTLP_ENDPOINT = $OtlpEndpoint
$env:TECFLIX_FFMPEG = (Get-Command ffmpeg -ErrorAction Stop).Source

& .\build-streaming\Release\tecflix_streaming.exe

if ($LASTEXITCODE) {
    throw 'Streaming server failed'
}

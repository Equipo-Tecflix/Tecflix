param([switch]$NoTests)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$tests = if ($NoTests) {'OFF'} else {'ON'}
cmake -S streaming-api -B build-streaming -G 'Visual Studio 17 2022' -A x64 "-DTECFLIX_BUILD_TESTS=$tests"
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
cmake --build build-streaming --config Release --parallel
if ($LASTEXITCODE) { throw 'Compilation failed' }
if (!$NoTests) {
    ctest --test-dir build-streaming -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
}

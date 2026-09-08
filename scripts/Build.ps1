[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    cmake -S . -B build -G 'Visual Studio 17 2022' -A Win32
    if ($LASTEXITCODE) { throw 'CMake configure failed' }
    cmake --build build --config $Configuration
    if ($LASTEXITCODE) { throw 'Build failed' }
    ctest --test-dir build -C $Configuration --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
} finally { Pop-Location }

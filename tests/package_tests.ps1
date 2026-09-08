[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Archive,[switch]$VerifyModes,[string]$DiscoveryFixtureBinary='')
$ErrorActionPreference='Stop'
$archivePath=(Resolve-Path -LiteralPath $Archive).Path
$zipHash=(Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if(!([IO.File]::ReadAllText(($archivePath+'.sha256'))).StartsWith($zipHash+'  ')) { throw 'Archive checksum mismatch' }
$testRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'temp'))+'\'
$fixture=Join-Path $testRoot ('package-verify-'+[guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $archivePath -DestinationPath $fixture
$package=Join-Path $fixture ([IO.Path]::GetFileNameWithoutExtension($archivePath))
$lines=[IO.File]::ReadAllLines((Join-Path $package 'SHA256SUMS.txt'))
$seen=@{}
foreach($line in $lines) {
    if($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Malformed file checksum' }
    $expected=$Matches[1]; $relative=$Matches[2]
    $file=[IO.Path]::GetFullPath((Join-Path $package $relative))
    if(!$file.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Checksum path escaped package' }
    if($seen.ContainsKey($relative) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expected) { throw ('File hash mismatch: '+$relative) }
    $seen[$relative]=$true
}
if(@(Get-ChildItem -LiteralPath $package -Recurse -File).Count -ne $lines.Count+1) { throw 'Unlisted package files' }
$exe=Join-Path $package 'CZ_Help.exe'
& (Join-Path $PSScriptRoot 'native_session_tests.ps1') -BinaryPath $exe -VerifyModes:$VerifyModes
if($DiscoveryFixtureBinary) {
    & (Join-Path $PSScriptRoot 'game_discovery_ui_tests.ps1') -BinaryPath $exe -FixtureBinary $DiscoveryFixtureBinary
}
$version=(Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
$report="Version: $version`r`nSHA256: $zipHash`r`nAll $($lines.Count) file hashes and complete archive coverage: PASS.`r`nPackaged EXE automatic preparation, readiness, normal close and crash restoration: PASS.`r`n"
if($VerifyModes) { $report+="Two radio modes, preference persistence and default-disabled restart: PASS.`r`n" }
if($DiscoveryFixtureBinary) { $report+="Packaged EXE game-first discovery, Unicode path persistence, delayed preparation and restoration: PASS.`r`n" }
[IO.File]::WriteAllText((Join-Path (Split-Path -Parent $archivePath) ('CZ_Help-'+$version+'-verification.txt')),$report,[Text.Encoding]::UTF8)
$resolved=(Resolve-Path -LiteralPath $fixture).Path
if(!$resolved.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Test cleanup boundary mismatch' }
Remove-Item -LiteralPath $resolved -Recurse -Force
Write-Output $report

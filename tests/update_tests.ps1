$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $PSScriptRoot ('temp\update-' + [guid]::NewGuid().ToString('N'))
$fakeBuild = Join-Path $fixture 'build'
New-Item -ItemType Directory -Path (Join-Path $fixture 'czero\dlls'),(Join-Path $fakeBuild 'Release') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'hl.exe'),'fixture')
[IO.File]::WriteAllText((Join-Path $fixture 'czero\dlls\mp.dll'),'fixture')
$lib = Join-Path $fixture 'czero\liblist.gam'
$original = "game `"Condition Zero`"`r`ngamedll `"dlls\mp.dll`"`r`n"
[IO.File]::WriteAllText($lib,$original)
$binary = Join-Path $fakeBuild 'Release\cz_help_mm.dll'
[IO.File]::WriteAllText($binary,'version 1')
& (Join-Path $repo 'scripts\Update.ps1') -GamePath $fixture -BuildDir $fakeBuild
$manifest = Join-Path $fixture 'czero\addons\cz_help\install-state.json'
$before = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$installed = Join-Path $fixture 'czero\addons\cz_help\cz_help_mm.dll'
[IO.File]::WriteAllText($binary,'version 2')
& (Join-Path $repo 'scripts\Update.ps1') -GamePath $fixture -BuildDir $fakeBuild
if ([IO.File]::ReadAllText($installed) -cne 'version 2') { throw 'FAIL: update did not replace plugin' }
$after = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
foreach ($record in $before.Files) {
    $updated = @($after.Files | Where-Object Relative -CEQ $record.Relative)
    if ($updated.Count -ne 1 -or $updated[0].Original -cne $record.Original) { throw 'FAIL: update lost original backup' }
}
$manifestBytes = [IO.File]::ReadAllText($manifest)
& (Join-Path $repo 'scripts\Update.ps1') -GamePath $fixture -BuildDir $fakeBuild
if ([IO.File]::ReadAllText($manifest) -cne $manifestBytes) { throw 'FAIL: repeat update rewrote manifest' }
[IO.File]::AppendAllText($lib,'// user change')
$refused = $false
try { & (Join-Path $repo 'scripts\Update.ps1') -GamePath $fixture -BuildDir $fakeBuild } catch { $refused = $true }
if (!$refused -or [IO.File]::ReadAllText($installed) -cne 'version 2') { throw 'FAIL: update ignored later user change' }
[IO.File]::WriteAllText($lib,($original -replace 'dlls\\mp.dll','addons/cz_help/metamod.dll'))
& (Join-Path $repo 'scripts\Uninstall.ps1') -GamePath $fixture
if ([IO.File]::ReadAllText($lib) -cne $original -or (Test-Path -LiteralPath $installed)) { throw 'FAIL: uninstall after update failed' }
$resolved = (Resolve-Path -LiteralPath $fixture).Path
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'temp')) + '\'
if (!$resolved.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture boundary mismatch' }
Remove-Item -LiteralPath $resolved -Recurse -Force
Write-Output 'PASS: fresh install via updater, update, original backup, repeat update, edit conflict, uninstall after update'

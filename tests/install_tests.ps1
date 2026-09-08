$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$install = Join-Path $repo 'scripts\Install.ps1'
$uninstall = Join-Path $repo 'scripts\Uninstall.ps1'
if (!(Test-Path -LiteralPath $install) -or !(Test-Path -LiteralPath $uninstall)) { throw 'FAIL: installer and restore behaviors are missing' }
$fixture = Join-Path $PSScriptRoot ('temp\install-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'czero\dlls') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'hl.exe'), 'fixture')
[IO.File]::WriteAllText((Join-Path $fixture 'czero\dlls\mp.dll'), 'fixture')
$lib = Join-Path $fixture 'czero\liblist.gam'
$original = "game `"Condition Zero`"`r`ngamedll `"dlls\mp.dll`"`r`n"
[IO.File]::WriteAllText($lib, $original)
$plugins = Join-Path $fixture 'czero\addons\metamod\plugins.ini'
New-Item -ItemType Directory -Path (Split-Path $plugins) -Force | Out-Null
[IO.File]::WriteAllText($plugins, "// user plugin comment`r`n")
& $install -GamePath $fixture
if ([IO.File]::ReadAllText($lib) -notmatch 'addons/cz_help/metamod.dll') { throw 'FAIL: game DLL was not routed through Metamod' }
if ([IO.File]::ReadAllText($plugins) -notmatch '// user plugin comment') { throw 'FAIL: existing plugin content lost' }
$first = [IO.File]::ReadAllText($plugins)
& $install -GamePath $fixture
if ([IO.File]::ReadAllText($plugins) -cne $first) { throw 'FAIL: repeated install changed plugins' }
& $uninstall -GamePath $fixture
if ([IO.File]::ReadAllText($lib) -cne $original) { throw 'FAIL: original game config not restored exactly' }
if ([IO.File]::ReadAllText($plugins) -cne "// user plugin comment`r`n") { throw 'FAIL: original plugins not restored exactly' }
& $install -GamePath $fixture
[IO.File]::AppendAllText($lib, '// user later change')
$conflict = $false
try { & $uninstall -GamePath $fixture } catch { $conflict = $true }
if (!$conflict -or [IO.File]::ReadAllText($lib) -notmatch 'user later change') { throw 'FAIL: later edits were not protected' }
# Return to exactly the installed content and prove restore remains usable after refusal.
$state = Get-Content -LiteralPath (Join-Path $fixture 'czero\addons\cz_help\install-state.json') -Raw | ConvertFrom-Json
[IO.File]::WriteAllText($lib, ($original -replace 'dlls\\mp.dll','addons/cz_help/metamod.dll'), [Text.Encoding]::ASCII)
& $uninstall -GamePath $fixture
# Clean only this freshly created fixture, after checking its resolved absolute boundary.
$resolvedFixture = (Resolve-Path -LiteralPath $fixture).Path
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'temp')) + [IO.Path]::DirectorySeparatorChar
if (!$resolvedFixture.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture boundary mismatch' }
Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
Write-Output 'PASS: install, idempotence, other plugins, exact restore, edit conflict, retry restore'

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Get-ReleaseFile([string]$Repo,[string]$Name,[string]$Configuration,[string]$BuildDir) {
    if (!$BuildDir) {
        $rootFile = Join-Path $Repo $Name
        if (Test-Path -LiteralPath $rootFile -PathType Leaf) { return $rootFile }
        $packed = Join-Path $Repo ('bin\' + $Name)
        if (Test-Path -LiteralPath $packed -PathType Leaf) { return $packed }
        $BuildDir = Join-Path $Repo 'build'
    }
    $file = Join-Path $BuildDir ($Configuration + '\' + $Name)
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing $Name. Build the project or extract the full release package." }
    return $file
}
function Get-GameRoot([string]$GamePath,[switch]$AllowRunning) {
    $root = (Resolve-Path -LiteralPath $GamePath -ErrorAction Stop).Path.TrimEnd('\','/')
    foreach ($relative in @('hl.exe','czero\liblist.gam','czero\dlls\mp.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) { throw "Missing game file: $relative" }
    }
    if (!$AllowRunning -and (Test-GameRunning $root)) { throw 'Exit this game before installing or restoring.' }
    return $root
}
function Test-GameRunning([string]$Root) {
    # An identically named executable in a different installation is unrelated.
    $exe = Join-Path $Root 'hl.exe'
    foreach ($process in @(Get-Process -Name hl -ErrorAction SilentlyContinue)) {
        try { $processPath = $process.Path } catch { throw 'Cannot inspect a running hl.exe. Exit the game first.' }
        if (!$processPath) { throw 'Cannot inspect a running hl.exe. Exit the game first.' }
        if ($processPath -ieq $exe) { return $true }
    }
    return $false
}
function Get-OwnedPath([string]$Root,[string]$Relative) {
    $allowed = @('czero\liblist.gam','czero\addons\metamod\plugins.ini','czero\addons\cz_help\metamod.dll','czero\addons\cz_help\cz_help_mm.dll')
    if ($Relative -cnotin $allowed) { throw "Unrecognized install record path: $Relative" }
    $target = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (!$target.StartsWith(($Root + '\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Target escaped game root' }
    # Junctions/symlinks in the mutation path must not redirect writes elsewhere.
    $walk = Split-Path -Parent $target
    while ($walk.Length -ge $Root.Length) {
        if (Test-Path -LiteralPath $walk) {
            if ((Get-Item -LiteralPath $walk -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in install path: $walk" }
        }
        if ($walk -ieq $Root) { break }
        $walk = Split-Path -Parent $walk
    }
    if ((Test-Path -LiteralPath $target) -and ((Get-Item -LiteralPath $target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Reparse target refused' }
    return $target
}
function Get-ByteHash([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','') } finally { $sha.Dispose() }
}
function Set-AtomicFile([string]$Path,[byte[]]$Bytes) {
    # Keep the previous complete file until the replacement has been fully flushed.
    # A process interruption cannot turn a tracked DLL or manifest into partial bytes.
    $temporary = $Path + '.CZHelp.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        $stream = [IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $stream.Write($Bytes,0,$Bytes.Length); $stream.Flush($true) }
        finally { $stream.Dispose() }
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary,$Path,[NullString]::Value) }
        else { [IO.File]::Move($temporary,$Path) }
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}
function Set-InstallRecord($State,[string]$Path) {
    $json = $State | ConvertTo-Json -Depth 6
    Set-AtomicFile $Path ([Text.Encoding]::UTF8.GetBytes($json))
}
function Assert-InstallRecord($State,[string]$Root) {
    if ($State.Version -ne 1 -or $State.GameRoot -ine $Root) { throw 'Install record does not match this game root' }
    $seen = @{}
    foreach ($file in $State.Files) {
        $null = Get-OwnedPath $Root $file.Relative
        if ($seen.ContainsKey($file.Relative)) { throw 'Install record contains duplicate paths' }
        $seen[$file.Relative] = $true
        if ($file.InstalledHash -cnotmatch '^[A-F0-9]{64}$') { throw 'Install record contains an invalid hash' }
        if ($file.PSObject.Properties['PendingHash'] -and ($file.Relative -cne 'czero\addons\cz_help\cz_help_mm.dll' -or $file.PendingHash -cnotmatch '^[A-F0-9]{64}$')) { throw 'Install record contains an invalid pending update hash' }
        if ($null -ne $file.Original) { $null = [Convert]::FromBase64String($file.Original) }
    }
    if (!$seen.ContainsKey('czero\addons\cz_help\cz_help_mm.dll')) { throw 'Install record is missing the CZ Help plugin' }
    if (!$seen.ContainsKey('czero\addons\metamod\plugins.ini')) { throw 'Install record is missing the plugins.ini backup' }
    if ($seen.ContainsKey('czero\liblist.gam') -and !$seen.ContainsKey('czero\addons\cz_help\metamod.dll')) { throw 'Install record is missing the Metamod loader backup' }
}
function Get-InstallRecord([string]$Root) {
    $null = Get-OwnedPath $Root 'czero\addons\cz_help\cz_help_mm.dll'
    $path = Join-Path $Root 'czero\addons\cz_help\install-state.json'
    if (!(Test-Path -LiteralPath $path)) { return $null }
    if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse install record refused' }
    $state = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-InstallRecord $state $Root
    return $state
}
function Assert-InstalledFiles($State,[string]$Root) {
    Assert-InstallRecord $State $Root
    foreach ($file in $State.Files) {
        $path = Get-OwnedPath $Root $file.Relative
        if (!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne $file.InstalledHash) {
            throw "File changed after installation; preserving it: $path. Resolve the change before restoring."
        }
    }
}
function Get-RestoreRecords($State,[string]$Root) {
    Assert-InstallRecord $State $Root
    $records = [Collections.Generic.List[object]]::new()
    foreach ($file in $State.Files) {
        $path = Get-OwnedPath $Root $file.Relative
        $exists = Test-Path -LiteralPath $path -PathType Leaf
        $hash = if ($exists) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash } else { $null }
        $restored = if ($null -eq $file.Original) { !(Test-Path -LiteralPath $path) } else { $exists -and $hash -ceq (Get-ByteHash ([Convert]::FromBase64String($file.Original))) }
        $pending = $file.PSObject.Properties['PendingHash'] -and $hash -ceq $file.PendingHash
        if (!$restored -and (!$exists -or ($hash -cne $file.InstalledHash -and !$pending))) {
            throw "File changed after installation; preserving it: $path. Resolve the change before restoring."
        }
        $records.Add([pscustomobject]@{Path=$path; Restored=$restored; Original=$file.Original})
    }
    # Complete preflight before the caller mutates the first file.
    return $records
}

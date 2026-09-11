[CmdletBinding()]
param(
    [string]$GamePath = 'D:\SteamLibrary\steamapps\common\Half-Life',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$BuildDir = '',
    [Parameter(Mandatory=$true)][int]$OwnerPid,
    [Parameter(Mandatory=$true)][long]$OwnerStartTime,
    [Parameter(Mandatory=$true)][string]$StatusPath,
    [switch]$RemoveStatusOnSuccess
)
# A native launcher can inherit PowerShell 7 module paths. Windows PowerShell 5.1
# may then find an incompatible Utility module before its own Get-FileHash.
# This short-lived worker only needs the modules bundled with its own host.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
. (Join-Path $PSScriptRoot 'Common.ps1')
$script:lastStatus = ''
$mutex = $null
$locked = $false
$exitCode = 0
function Set-SessionStatus([string]$State,[string]$Detail = '') {
    $text = $State + "`n"
    if ($Detail) { $text += $Detail + "`n" }
    if ($text -ceq $script:lastStatus) { return }
    $temporary = $StatusPath + '.' + $PID + '.tmp'
    try {
        [IO.File]::WriteAllText($temporary,$text,[Text.UTF8Encoding]::new($false))
        for ($attempt = 0; $attempt -lt 10; $attempt++) {
            try {
                if (Test-Path -LiteralPath $StatusPath) { [IO.File]::Replace($temporary,$StatusPath,[NullString]::Value) }
                else { [IO.File]::Move($temporary,$StatusPath) }
                $script:lastStatus = $text
                return
            } catch [IO.IOException] {
                if ($attempt -eq 9) { throw }
                Start-Sleep -Milliseconds 40
            }
        }
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}
function Test-OwnerAlive {
    if ($OwnerPid -le 0 -or $OwnerStartTime -le 0) { return $false }
    $process = Get-Process -Id $OwnerPid -ErrorAction SilentlyContinue
    if (!$process) { return $false }
    try { return !$process.HasExited -and $process.StartTime.ToUniversalTime().ToFileTimeUtc() -eq $OwnerStartTime }
    catch { return $false }
    finally { $process.Dispose() }
}
function Test-MatchingInstall([string]$Root,[string]$PluginHash) {
    $state = Get-InstallRecord $Root
    if (!$state) { return $false }
    try { Assert-InstalledFiles $state $Root } catch { return $false }
    $pluginRecord = @($state.Files | Where-Object Relative -CEQ 'czero\addons\cz_help\cz_help_mm.dll')
    return $pluginRecord.Count -eq 1 -and $pluginRecord[0].InstalledHash -ceq $PluginHash
}
function Invoke-Session {
    Set-SessionStatus starting
    # Do not acquire or adopt a previous installation on behalf of a dead/reused PID.
    if (!(Test-OwnerAlive)) { Set-SessionStatus cleaned; return }
    $root = Get-GameRoot $GamePath -AllowRunning
    $repo = Split-Path -Parent $PSScriptRoot
    $plugin = Get-ReleaseFile $repo 'cz_help_mm.dll' $Configuration $BuildDir
    $hash = (Get-FileHash -LiteralPath $plugin -Algorithm SHA256).Hash
    $rootHash = Get-ByteHash ([Text.Encoding]::Unicode.GetBytes($root.ToUpperInvariant()))
    $script:mutex = [Threading.Mutex]::new($false,('Local\CZHelp.Session.v1.' + $rootHash))
    if (!(Test-OwnerAlive)) { Set-SessionStatus cleaned; return }
    while (!$script:locked) {
        try { $script:locked = $script:mutex.WaitOne(200) }
        catch [Threading.AbandonedMutexException] { $script:locked = $true }
        if (!$script:locked) {
            Set-SessionStatus waiting_cleanup
            if (!(Test-OwnerAlive)) { Set-SessionStatus cleaned; return }
        }
    }
    if (!(Test-OwnerAlive)) { Set-SessionStatus cleaned; return }
    $managed = $false
    while (Test-OwnerAlive) {
        if (Test-GameRunning $root) {
            if (Test-MatchingInstall $root $hash) {
                $managed = $true
                Set-SessionStatus ready
                break
            }
            Set-SessionStatus waiting_game_exit
            Start-Sleep -Milliseconds 200
            continue
        }
        Set-SessionStatus preparing
        # Restore complete or interrupted old records first. This retains the original
        # pre-1.2 bytes, and avoids adopting an intermediate update as a new original.
        if (Get-InstallRecord $root) {
            & (Join-Path $PSScriptRoot 'Uninstall.ps1') -GamePath $root | Out-Null
        }
        if (!(Test-OwnerAlive)) { break }
        & (Join-Path $PSScriptRoot 'Install.ps1') -GamePath $root -Configuration $Configuration -BuildDir $BuildDir | Out-Null
        $managed = $true
        Set-SessionStatus ready
        break
    }
    if ($managed) {
        while (Test-OwnerAlive) { Start-Sleep -Milliseconds 200 }
        # Loaded DLLs remain inactive after the UI heartbeat stops. Restore only once
        # this installation's game is gone; never terminate the user's process.
        while (Test-GameRunning $root) {
            Set-SessionStatus waiting_game_exit
            Start-Sleep -Milliseconds 200
        }
        Set-SessionStatus cleaning
        & (Join-Path $PSScriptRoot 'Uninstall.ps1') -GamePath $root | Out-Null
    }
    Set-SessionStatus cleaned
}
try {
    # TEMP can contain an 8.3 alias; .NET expands it even though it names the
    # same file. Normalize once, but still reject drive/current-root relative paths.
    if ([IO.Path]::GetPathRoot($StatusPath) -notmatch '^(?:[A-Za-z]:[\\/]|[\\/]{2})') { throw 'StatusPath must be an absolute file path.' }
    $StatusPath = [IO.Path]::GetFullPath($StatusPath)
    if (!(Test-Path -LiteralPath (Split-Path -Parent $StatusPath) -PathType Container)) { throw 'Status file directory does not exist.' }
    Invoke-Session
    if ($RemoveStatusOnSuccess -and (Test-Path -LiteralPath $StatusPath)) { Remove-Item -LiteralPath $StatusPath }
} catch {
    $exitCode = 1
    $failure = $_.Exception.Message
    try { Set-SessionStatus error $failure } catch { Write-Error $failure -ErrorAction Continue }
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}
exit $exitCode

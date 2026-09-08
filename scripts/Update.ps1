[CmdletBinding()]
param([string]$GamePath = 'D:\SteamLibrary\steamapps\common\Half-Life',
      [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
      [string]$BuildDir = '')
. (Join-Path $PSScriptRoot 'Common.ps1')
$root = Get-GameRoot $GamePath
$repo = Split-Path -Parent $PSScriptRoot
$target = Get-OwnedPath $root 'czero\addons\cz_help\cz_help_mm.dll'
$statePath = Join-Path $root 'czero\addons\cz_help\install-state.json'
if (!(Test-Path -LiteralPath $statePath)) {
    & (Join-Path $PSScriptRoot 'Install.ps1') -GamePath $root -Configuration $Configuration -BuildDir $BuildDir
    return
}
$stateBytes = [IO.File]::ReadAllBytes($statePath)
$state = Get-InstallRecord $root
Assert-InstalledFiles $state $root
$records = @($state.Files | Where-Object Relative -CEQ 'czero\addons\cz_help\cz_help_mm.dll')
if ($records.Count -ne 1) { throw 'Install record must contain exactly one CZ Help plugin entry.' }
$plugin = Get-ReleaseFile $repo 'cz_help_mm.dll' $Configuration $BuildDir
$newBytes = [IO.File]::ReadAllBytes($plugin)
$newHash = Get-ByteHash $newBytes
if ($records[0].InstalledHash -ceq $newHash) { Write-Output 'This plugin build is already installed.'; return }
$oldBytes = [IO.File]::ReadAllBytes($target)
# Persist both valid DLL hashes before replacing either file. If the process stops
# between the DLL and final manifest write, restoration can still recognize it.
$records[0] | Add-Member -NotePropertyName PendingHash -NotePropertyValue $newHash -Force
try {
    Set-InstallRecord $state $statePath
    Set-AtomicFile $target $newBytes
    # Retain Original from the first installation.
    $records[0].InstalledHash = $newHash
    $records[0].PSObject.Properties.Remove('PendingHash')
    Set-InstallRecord $state $statePath
} catch {
    $updateError = $_
    Set-AtomicFile $target $oldBytes
    Set-AtomicFile $statePath $stateBytes
    throw $updateError
}
Write-Output "Updated CZ Help plugin in $root. Original uninstall backup retained."

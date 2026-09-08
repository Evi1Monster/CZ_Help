[CmdletBinding()]
param([string]$GamePath='D:\SteamLibrary\steamapps\common\Half-Life',
      [ValidateSet('Debug','Release')][string]$Configuration='Release',
      [switch]$Windowed)
. (Join-Path $PSScriptRoot 'Common.ps1')
$root=(Resolve-Path -LiteralPath $GamePath).Path.TrimEnd('\','/')
$repo=Split-Path -Parent $PSScriptRoot
$exe=Get-ReleaseFile $repo 'CZ_Help.exe' $Configuration ''
if (!(Test-Path -LiteralPath (Join-Path $root 'hl.exe'))) { throw 'Game executable not found.' }
if (@(Get-Process -Name CZ_Help -ErrorAction SilentlyContinue).Count) {
    throw 'CZ Help is already running. Use its Start Local Game button, or close it before switching releases.'
}
# The UI launches the game only after background preparation reports ready.
$arguments='--game-path "' + $root + '" --launch-game'
if ($Windowed) { $arguments += ' --windowed' }
Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Normal
Write-Output 'CZ Help will configure automatically, then start the local game. Closing it schedules automatic restoration.'

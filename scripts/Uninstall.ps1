[CmdletBinding()]
param([string]$GamePath = 'D:\SteamLibrary\steamapps\common\Half-Life')
. (Join-Path $PSScriptRoot 'Common.ps1')
$root = Get-GameRoot $GamePath
$null = Get-OwnedPath $root 'czero\addons\cz_help\cz_help_mm.dll'
$statePath = Join-Path $root 'czero\addons\cz_help\install-state.json'
if (!(Test-Path -LiteralPath $statePath)) { Write-Output 'No CZ Help installation record. Nothing changed.'; return }
$state = Get-InstallRecord $root
# Check every file before restoring any of them.
$records = @(Get-RestoreRecords $state $root)
foreach ($file in $records) {
    if ($file.Restored) { continue }
    if ($null -ne $file.Original) { Set-AtomicFile $file.Path ([Convert]::FromBase64String($file.Original)) }
    else { Remove-Item -LiteralPath $file.Path }
}
Remove-Item -LiteralPath $statePath
Write-Output 'Restored original files. Unrelated files and directories were preserved.'

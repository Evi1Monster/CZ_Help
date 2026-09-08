[CmdletBinding()]
param([string]$GamePath = 'D:\SteamLibrary\steamapps\common\Half-Life',
      [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
      [string]$BuildDir = '')
. (Join-Path $PSScriptRoot 'Common.ps1')
$root = Get-GameRoot $GamePath
$repo = Split-Path -Parent $PSScriptRoot
$statePath = Join-Path $root 'czero\addons\cz_help\install-state.json'
# Validate the manifest parent before reading or creating it.
$null = Get-OwnedPath $root 'czero\addons\cz_help\cz_help_mm.dll'
if (Test-Path -LiteralPath $statePath) {
    $state = Get-InstallRecord $root
    Assert-InstalledFiles $state $root
    Write-Output 'Already installed. Use Update.ps1 to update the plugin.'
    return
}
$plugin = Get-ReleaseFile $repo 'cz_help_mm.dll' $Configuration $BuildDir
$runtime = Join-Path $repo 'third_party\metamod-runtime\addons\metamod\dlls\metamod.dll'
if (!(Test-Path -LiteralPath $plugin)) { throw 'Build the plugin first with scripts/Build.ps1.' }
if (!(Test-Path -LiteralPath $runtime) -or (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -cne '16B849F1CBF1503266A1B89D9B4ED38807091FABE724A8607D544338D155A005') { throw 'Official Metamod runtime is missing or has an unexpected hash.' }
$changes = [Collections.Generic.List[object]]::new()
function Add-Change([string]$Relative,[byte[]]$Bytes) {
    $path = Get-OwnedPath $root $Relative
    $before = if (Test-Path -LiteralPath $path) { [Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) } else { $null }
    $changes.Add([pscustomobject]@{Relative=$Relative; Original=$before; InstalledHash=(Get-ByteHash $Bytes); Content=$Bytes})
}
$libPath = Get-OwnedPath $root 'czero\liblist.gam'
$byteEncoding = [Text.Encoding]::GetEncoding(28591)
$lib = $byteEncoding.GetString([IO.File]::ReadAllBytes($libPath))
$match = [regex]::Match($lib,'(?m)^gamedll\s+"([^"\r\n]+)"')
if (!$match.Success) { throw 'No Windows gamedll entry in liblist.gam' }
$currentDll = $match.Groups[1].Value.Replace('/','\')
if ($currentDll -ieq 'dlls\mp.dll') {
    $newLib = $lib.Substring(0,$match.Groups[1].Index) + 'addons/cz_help/metamod.dll' + $lib.Substring($match.Groups[1].Index+$match.Groups[1].Length)
    Add-Change 'czero\liblist.gam' ($byteEncoding.GetBytes($newLib))
    Add-Change 'czero\addons\cz_help\metamod.dll' ([IO.File]::ReadAllBytes($runtime))
} elseif ($currentDll -match 'metamod[^\\]*\.dll$' -and (Test-Path -LiteralPath (Join-Path (Join-Path $root 'czero') $currentDll))) {
    Write-Output 'Keeping the existing Metamod loader.'
} else { throw "Unsupported existing gamedll: $currentDll. No files were changed." }
$pluginsPath = Get-OwnedPath $root 'czero\addons\metamod\plugins.ini'
$plugins = if (Test-Path -LiteralPath $pluginsPath) { $byteEncoding.GetString([IO.File]::ReadAllBytes($pluginsPath)) } else { '' }
if ($plugins -match '(?im)^\s*win32\s+addons/cz_help/cz_help_mm\.dll\b') { throw 'An untracked CZ Help plugin entry already exists; preserve it and resolve before installing.' }
if ($plugins.Length -and !$plugins.EndsWith("`n")) { $plugins += "`r`n" }
$plugins += "win32 addons/cz_help/cz_help_mm.dll`r`n"
Add-Change 'czero\addons\metamod\plugins.ini' ($byteEncoding.GetBytes($plugins))
Add-Change 'czero\addons\cz_help\cz_help_mm.dll' ([IO.File]::ReadAllBytes($plugin))
$state = [pscustomobject]@{Version=1; GameRoot=$root; Files=@($changes | Select-Object Relative,Original,InstalledHash)}
New-Item -ItemType Directory -Path (Split-Path -Parent $statePath) -Force | Out-Null
Set-InstallRecord $state $statePath
try {
    foreach ($change in $changes) {
        $path = Get-OwnedPath $root $change.Relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        Set-AtomicFile $path $change.Content
    }
} catch {
    $installError = $_
    foreach ($change in $changes) {
        $path = Get-OwnedPath $root $change.Relative
        if ($null -ne $change.Original) { Set-AtomicFile $path ([Convert]::FromBase64String($change.Original)) }
        elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
    Remove-Item -LiteralPath $statePath
    throw $installError
}
Write-Output "Installed into $root. Original file bytes saved in $statePath"

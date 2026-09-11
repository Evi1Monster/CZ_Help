[CmdletBinding()]
param([ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '1.4.3',
      [string]$OutputDirectory = '')
. (Join-Path $PSScriptRoot 'Common.ps1')
$repo = Split-Path -Parent $PSScriptRoot
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist' }
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$name = 'CZ_Help-' + $Version + '-win32'
$package = Join-Path $OutputDirectory $name
$zip = $package + '.zip'
if ((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath $zip)) { throw 'Release output already exists. Choose a new output directory to avoid replacing it.' }
foreach ($binary in @('CZ_Help.exe','cz_help_mm.dll')) { $null = Get-ReleaseFile $repo $binary 'Release' (Join-Path $repo 'build') }
$releaseNotes = Join-Path $repo ('docs\release-' + $Version + '.md')
if (!(Test-Path -LiteralPath $releaseNotes)) { throw 'Release notes are missing.' }
if ((Get-Item -LiteralPath (Join-Path $repo 'build\Release\CZ_Help.exe')).VersionInfo.ProductVersion -cne $Version) { throw 'Built EXE version does not match requested package version.' }
New-Item -ItemType Directory -Path (Join-Path $package 'bin'),(Join-Path $package 'scripts') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'build\Release\CZ_Help.exe') -Destination $package
Copy-Item -LiteralPath (Join-Path $repo 'build\Release\cz_help_mm.dll') -Destination (Join-Path $package 'bin')
$defaultIni = "[Game]`r`nPath=D:\SteamLibrary\steamapps\common\Half-Life`r`n[Hotkeys]`r`nToggle=119`r`nBoxes=120`r`nHealth=122`r`nBhopToggle=114`r`n[Display]`r`nBoxes=1`r`nHealth=1`r`n[Movement]`r`nMode=0`r`n"
[IO.File]::WriteAllText((Join-Path $package 'CZ_Help.ini'),$defaultIni,[Text.Encoding]::Unicode)
foreach ($script in @('Common.ps1','Install.ps1','Update.ps1','Uninstall.ps1','Launch.ps1','Session.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination (Join-Path $package 'scripts')
}
$runtimeTarget = Join-Path $package 'third_party\metamod-runtime\addons\metamod\dlls'
New-Item -ItemType Directory -Path $runtimeTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'third_party\metamod-runtime\addons\metamod\dlls\metamod.dll') -Destination $runtimeTarget
Copy-Item -LiteralPath (Join-Path $repo 'third_party\metamod-runtime\SOURCE.md') -Destination (Join-Path $package 'third_party\metamod-runtime')
New-Item -ItemType Directory -Path (Join-Path $package 'third_party\metamod') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'third_party\metamod\GPL.txt') -Destination (Join-Path $package 'third_party\metamod')
Copy-Item -LiteralPath $releaseNotes -Destination (Join-Path $package 'QuickStart.zh-CN.md')
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination $package
# Include matching buildable sources and unchanged upstream license notices.
$sourceRoot = Join-Path $package 'source'
$sourceDirs = @('CZ_Help','plugin','shared','scripts','tests','third_party','docs')
foreach ($dir in $sourceDirs) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo $dir) -Recurse -File) {
        $relative = $file.FullName.Substring($repo.Length + 1)
        if ($relative -match '(^|\\)(Debug|Release|x64|x86|obj|bin|temp|\.vs)(\\|$)' -or $relative -match '\.(user|pdb|obj|log|xz)$') { continue }
        $target = Join-Path $sourceRoot $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}
foreach ($file in @('CMakeLists.txt','CZ_Help.sln','README.md')) { Copy-Item -LiteralPath (Join-Path $repo $file) -Destination $sourceRoot }
$hashes = foreach ($file in Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName) {
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $file.FullName.Substring($package.Length + 1).Replace('\','/')
}
[IO.File]::WriteAllLines((Join-Path $package 'SHA256SUMS.txt'),[string[]]$hashes,[Text.Encoding]::ASCII)
Compress-Archive -LiteralPath $package -DestinationPath $zip -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($zip + '.sha256'),($zipHash + '  ' + [IO.Path]::GetFileName($zip) + "`r`n"),[Text.Encoding]::ASCII)
Write-Output "Release package: $zip"
Write-Output "SHA256: $zipHash"

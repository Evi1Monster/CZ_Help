[CmdletBinding()]
param([string]$ScriptsDirectory = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$ScriptsDirectory) { $ScriptsDirectory = Join-Path $repo 'scripts' }
$install = Join-Path $ScriptsDirectory 'Install.ps1'
$uninstall = Join-Path $ScriptsDirectory 'Uninstall.ps1'
$update = Join-Path $ScriptsDirectory 'Update.ps1'
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'temp')) + '\'
$fixture = Join-Path $testRoot ('yapb-install-' + [guid]::NewGuid().ToString('N'))
$fakeBuild = Join-Path $fixture 'build'
New-Item -ItemType Directory -Path (Join-Path $fakeBuild 'Release') -Force | Out-Null
$pluginBinary = Join-Path $fakeBuild 'Release\cz_help_mm.dll'
[IO.File]::WriteAllText($pluginBinary,'test plugin v1')
$encoding = [Text.Encoding]::GetEncoding(28591)
function Require([bool]$Condition,[string]$Message) { if (!$Condition) { throw ('FAIL: ' + $Message) } }
function New-Game([string]$Name,[string]$Entry,[bool]$WithYaPB = $true) {
    $game = Join-Path $fixture $Name
    New-Item -ItemType Directory -Path (Join-Path $game 'czero\dlls') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $game 'hl.exe'),'fixture, never executed')
    [IO.File]::WriteAllText((Join-Path $game 'czero\dlls\mp.dll'),'original game DLL')
    $lib = "// preserve bytes: $([char]0xE9)`r`ngame `"Condition Zero`"`r`ngamedll `"$Entry`"`r`ngamedll_linux `"dlls/cs_i386.so`"`r`n"
    [IO.File]::WriteAllBytes((Join-Path $game 'czero\liblist.gam'),$encoding.GetBytes($lib))
    if ($WithYaPB) {
        $yapb = Join-Path $game 'czero\addons\yapb\bin\yapb.dll'
        New-Item -ItemType Directory -Path (Split-Path -Parent $yapb) -Force | Out-Null
        [IO.File]::WriteAllText($yapb,'original YaPB DLL, never executed')
        [IO.File]::WriteAllText((Join-Path $game 'czero\addons\yapb\yapb.cfg'),'task pack bot settings')
    }
    return $game
}
function Snapshot([string]$Game) {
    return (@(Get-ChildItem -LiteralPath $Game -Recurse -File | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($Game.Length) + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }) -join "`n")
}
try {
    # A missing migration branch, dropped bot entry, duplicate entry or lost backup must fail here.
    $cases = @(
        @{Name='forward'; Entry='addons/yapb/bin/yapb.dll'; Plugins=$null;
            Expected="win32 addons/yapb/bin/yapb.dll`r`nwin32 addons/cz_help/cz_help_mm.dll`r`n"},
        @{Name='backslash'; Entry='ADDONS\YAPB\BIN\YAPB.DLL'; Plugins="// other plugin`r`nwin32 addons/other/other.dll";
            Expected="// other plugin`r`nwin32 addons/other/other.dll`r`nwin32 addons/yapb/bin/yapb.dll`r`nwin32 addons/cz_help/cz_help_mm.dll`r`n"},
        @{Name='existing'; Entry='addons/yapb/bin/yapb.dll'; Plugins="  WIN32 addons\YaPB\bin\YaPB.dll YaPB bots`r`n";
            Expected="  WIN32 addons\YaPB\bin\YaPB.dll YaPB bots`r`nwin32 addons/cz_help/cz_help_mm.dll`r`n"},
        @{Name='split-entry'; Entry='addons/yapb/bin/yapb.dll'; Plugins="win32`r`naddons/yapb/bin/yapb.dll`r`n";
            Expected="win32`r`naddons/yapb/bin/yapb.dll`r`nwin32 addons/yapb/bin/yapb.dll`r`nwin32 addons/cz_help/cz_help_mm.dll`r`n"},
        @{Name='commented'; Entry='addons/yapb/bin/yapb.dll'; Plugins=";win32 addons/yapb/bin/yapb.dll`r`nwin32 addons/yapb/bin/yapb.dll.backup`r`n";
            Expected=";win32 addons/yapb/bin/yapb.dll`r`nwin32 addons/yapb/bin/yapb.dll.backup`r`nwin32 addons/yapb/bin/yapb.dll`r`nwin32 addons/cz_help/cz_help_mm.dll`r`n"}
    )
    foreach ($case in $cases) {
        [IO.File]::WriteAllText($pluginBinary,'test plugin v1')
        $game = New-Game $case.Name $case.Entry
        $lib = Join-Path $game 'czero\liblist.gam'
        $plugins = Join-Path $game 'czero\addons\metamod\plugins.ini'
        if ($null -ne $case.Plugins) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $plugins) -Force | Out-Null
            [IO.File]::WriteAllText($plugins,$case.Plugins,[Text.Encoding]::ASCII)
        }
        $baseline = Snapshot $game
        & $install -GamePath $game -BuildDir $fakeBuild | Out-Null
        Require ([IO.File]::ReadAllText($lib) -match 'gamedll "addons/cz_help/metamod.dll"') 'YaPB entry was not routed through Metamod'
        $configured = [IO.File]::ReadAllText($plugins)
        Require ($configured -ceq $case.Expected) 'plugin list must preserve existing bytes and load both bots and CZ Help without adding duplicates'
        Require ([IO.File]::ReadAllText((Join-Path $game 'czero\addons\yapb\bin\yapb.dll')) -ceq 'original YaPB DLL, never executed') 'install changed the bot DLL'
        Require ([IO.File]::ReadAllText((Join-Path $game 'czero\addons\yapb\yapb.cfg')) -ceq 'task pack bot settings') 'install changed the bot settings'
        $installed = Snapshot $game
        & $install -GamePath $game -BuildDir $fakeBuild | Out-Null
        Require ((Snapshot $game) -ceq $installed) 'repeated install changed files or backup'
        [IO.File]::WriteAllText($pluginBinary,'test plugin updated')
        & $update -GamePath $game -BuildDir $fakeBuild | Out-Null
        & $uninstall -GamePath $game | Out-Null
        Require ((Snapshot $game) -ceq $baseline) 'uninstall after update did not restore all original bytes and file absence'
        Write-Output ('PASS: YaPB migration, idempotence, update and exact restore: ' + $case.Name)
    }
    # Missing bot binaries and unknown entry points must fail before the first write.
    foreach ($case in @(
        @{Name='missing'; Entry='addons/yapb/bin/yapb.dll'; Error='YaPB.*missing'},
        @{Name='unknown'; Entry='addons/unknown/unknown.dll'; Error='Unsupported existing gamedll'}
    )) {
        $game = New-Game $case.Name $case.Entry $false
        $baseline = Snapshot $game
        $failure = ''
        try { & $install -GamePath $game -BuildDir $fakeBuild | Out-Null } catch { $failure = $_.Exception.Message }
        Require ($failure -match $case.Error) ('expected actionable refusal: ' + $case.Name + '; got: ' + $failure)
        Require ((Snapshot $game) -ceq $baseline) 'refused install changed files'
        Write-Output ('PASS: refusal without mutation: ' + $case.Name)
    }
    # A later plugins.ini edit must prevent any restoration and retain the original backup.
    $game = New-Game 'conflict' 'addons/yapb/bin/yapb.dll'
    $baseline = Snapshot $game
    & $install -GamePath $game -BuildDir $fakeBuild | Out-Null
    $plugins = Join-Path $game 'czero\addons\metamod\plugins.ini'
    $installedPlugins = [IO.File]::ReadAllBytes($plugins)
    [IO.File]::AppendAllText($plugins,'// later user edit')
    $edited = Snapshot $game
    $refused = $false
    try { & $uninstall -GamePath $game | Out-Null } catch { $refused = $true }
    Require $refused 'restore ignored later plugin configuration edits'
    Require ((Snapshot $game) -ceq $edited) 'restore mutated files before rejecting later edits'
    [IO.File]::WriteAllBytes($plugins,$installedPlugins)
    & $uninstall -GamePath $game | Out-Null
    Require ((Snapshot $game) -ceq $baseline) 'restore retry lost YaPB original configuration'
    Write-Output 'PASS: YaPB restore conflict protection and retry'
} finally {
    $resolved = (Resolve-Path -LiteralPath $fixture).Path
    if (!$resolved.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolved) -notmatch '^yapb-install-[0-9a-f]{32}$') { throw 'Fixture cleanup boundary mismatch' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

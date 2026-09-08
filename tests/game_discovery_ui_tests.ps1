[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BinaryPath,[Parameter(Mandatory=$true)][string]$FixtureBinary)
$ErrorActionPreference='Stop'
$binary=(Resolve-Path -LiteralPath $BinaryPath).Path
$fixtureBinaryPath=(Resolve-Path -LiteralPath $FixtureBinary).Path
if (@(Get-Process -Name hl,CZ_Help -ErrorAction SilentlyContinue).Count) { throw 'Close games and CZ Help before isolated discovery UI tests; no user process was stopped.' }
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class CZDiscoveryUITest {
    public delegate bool Callback(IntPtr hwnd, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback fn, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string section, string key, string fallback, StringBuilder text, uint count, string path);
    public static IntPtr Panel(int wanted) {
        IntPtr result=IntPtr.Zero;
        EnumWindows((hwnd,state)=> { uint pid; GetWindowThreadProcessId(hwnd,out pid);
            var cls=new StringBuilder(128); GetClassName(hwnd,cls,128);
            if (pid==(uint)wanted && cls.ToString()=="CZHelpPanel") { result=hwnd; return false; } return true;
        },IntPtr.Zero); return result;
    }
    public static string Text(IntPtr window) { var text=new StringBuilder(4096); GetWindowText(window,text,4096); return text.ToString(); }
    public static string GamePath(string ini) { var text=new StringBuilder(32768); GetPrivateProfileString("Game","Path","",text,32768,ini); return text.ToString(); }
}
'@
$testRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'temp'))+'\'
$fixture=Join-Path $testRoot ('discovery & '+[char]0x6E38+[char]0x620F+' '+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'czero/dlls') -Force | Out-Null
Copy-Item -LiteralPath $fixtureBinaryPath -Destination (Join-Path $fixture 'hl.exe')
[IO.File]::WriteAllText((Join-Path $fixture 'czero/dlls/mp.dll'),'fixture')
$original="game `"Condition Zero`"`r`ngamedll `"dlls/mp.dll`"`r`n"
$libPath=Join-Path $fixture 'czero/liblist.gam'
$manifest=Join-Path $fixture 'czero/addons/cz_help/install-state.json'
[IO.File]::WriteAllText($libPath,$original)
$ini=Join-Path (Split-Path -Parent $binary) 'CZ_Help.ini'
$iniBackup=if(Test-Path -LiteralPath $ini) { [IO.File]::ReadAllBytes($ini) } else { $null }
$helper=$null; $gameFixture=$null
function Wait-Condition([scriptblock]$Condition,[string]$Message) {
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) { if (& $Condition) { return }; Start-Sleep -Milliseconds 100 }
    throw $Message
}
function Start-Helper {
    $script:helper=Start-Process -FilePath $binary -WindowStyle Hidden -PassThru
    Wait-Condition {
        $helper.Refresh()
        if($helper.HasExited) { throw 'Helper exited during discovery' }
        return [CZDiscoveryUITest]::Panel($helper.Id) -ne [IntPtr]::Zero
    } 'Helper window did not appear'
}
function Stop-Helper {
    $null=[CZDiscoveryUITest]::PostMessage([CZDiscoveryUITest]::Panel($helper.Id),0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    if(!$helper.WaitForExit(10000)) { throw 'Test helper did not exit' }
    $helper.Dispose(); $script:helper=$null
}
function Wait-Ready {
    $ready=[string][char]0x51C6+[char]0x5907+[char]0x5C31+[char]0x7EEA
    Wait-Condition { [CZDiscoveryUITest]::Text([CZDiscoveryUITest]::GetDlgItem([CZDiscoveryUITest]::Panel($helper.Id),109)).Contains($ready) } 'Helper did not become ready'
    if(!(Test-Path -LiteralPath $manifest)) { throw 'Ready without installed plugin record' }
}
try {
    # A bad saved path must be replaced by the actual process directory, without CLI overrides.
    [IO.File]::WriteAllText($ini,"[Game]`r`nPath=Z:\Missing\OldGame`r`n",[Text.Encoding]::Unicode)
    $gameFixture=Start-Process -FilePath (Join-Path $fixture 'hl.exe') -ArgumentList '--fixture -game "czero"' -WindowStyle Hidden -PassThru
    Start-Helper
    $pathText=[CZDiscoveryUITest]::Text([CZDiscoveryUITest]::GetDlgItem([CZDiscoveryUITest]::Panel($helper.Id),108))
    if(!$pathText.Contains($fixture)) { throw ('Running game path was not displayed: '+$pathText) }
    if([CZDiscoveryUITest]::GamePath($ini) -cne $fixture) { throw 'Automatically discovered path was not persisted' }
    $waiting=[string][char]0x7B49+[char]0x5F85+[char]0x6E38+[char]0x620F+[char]0x9000+[char]0x51FA
    Wait-Condition { [CZDiscoveryUITest]::Text([CZDiscoveryUITest]::GetDlgItem([CZDiscoveryUITest]::Panel($helper.Id),109)).Contains($waiting) } 'Already-running game did not show restart preparation status'
    if((Test-Path -LiteralPath $manifest) -or [IO.File]::ReadAllText($libPath) -cne $original) { throw 'Running unconfigured game was modified' }
    Stop-Process -InputObject $gameFixture -Force
    $null=$gameFixture.WaitForExit(5000); $gameFixture.Dispose(); $gameFixture=$null
    Wait-Ready
    Stop-Helper
    Wait-Condition { !(Test-Path -LiteralPath $manifest) } 'Discovered installation was not restored on helper exit'
    if([IO.File]::ReadAllText($libPath) -cne $original) { throw 'Original game configuration changed after cleanup' }
    # Next run has no game process and must reuse the remembered directory.
    Start-Helper
    Wait-Ready
    if(![CZDiscoveryUITest]::Text([CZDiscoveryUITest]::GetDlgItem([CZDiscoveryUITest]::Panel($helper.Id),108)).Contains($fixture)) { throw 'Remembered path was not displayed' }
    Stop-Helper
    Wait-Condition { !(Test-Path -LiteralPath $manifest) } 'Remembered installation was not restored'
    Write-Output 'PASS: game first + stale INI discovers and persists Unicode directory; waits without changing running game; prepares on game exit; restores on close; next launch reuses directory.'
} finally {
    foreach($owned in @($gameFixture,$helper)) {
        if($owned) { $owned.Refresh(); if(!$owned.HasExited) { Stop-Process -InputObject $owned -Force; $null=$owned.WaitForExit(5000) }; $owned.Dispose() }
    }
    try { Wait-Condition { !(Test-Path -LiteralPath $manifest) } 'Fixture cleanup is still pending' }
    finally {
        if($null -ne $iniBackup) { [IO.File]::WriteAllBytes($ini,$iniBackup) }
        elseif(Test-Path -LiteralPath $ini) { Remove-Item -LiteralPath $ini }
    }
    $resolved=(Resolve-Path -LiteralPath $fixture).Path
    if(!$resolved.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture cleanup boundary mismatch' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

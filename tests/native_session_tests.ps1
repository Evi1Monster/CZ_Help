[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BinaryPath,[switch]$VerifyModes)
$ErrorActionPreference='Stop'
$binary=(Resolve-Path -LiteralPath $BinaryPath).Path
if (@(Get-Process -Name CZ_Help -ErrorAction SilentlyContinue).Count) { throw 'Close the existing CZ Help before native lifecycle tests; no user process was stopped.' }
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class CZNativeSessionTest {
    public delegate bool Callback(IntPtr hwnd, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback fn, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hwnd, Callback fn, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    public static IntPtr Panel(int wanted) {
        IntPtr result=IntPtr.Zero;
        EnumWindows((hwnd,state)=> { uint pid; GetWindowThreadProcessId(hwnd,out pid);
            var cls=new StringBuilder(128); GetClassName(hwnd,cls,128);
            if (pid==(uint)wanted && cls.ToString()=="CZHelpPanel") { result=hwnd; return false; } return true;
        },IntPtr.Zero); return result;
    }
    public static string Text(IntPtr panel) {
        var result=new StringBuilder();
        EnumChildWindows(panel,(hwnd,state)=> { var text=new StringBuilder(4096); GetWindowText(hwnd,text,4096); result.AppendLine(text.ToString()); return true; },IntPtr.Zero);
        return result.ToString();
    }
}
'@
$testRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'temp'))+'\'
$fixture=Join-Path $testRoot ('native session & '+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'czero/dlls'),(Join-Path $fixture 'czero/addons/metamod') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'hl.exe'),'fixture, never executed')
[IO.File]::WriteAllText((Join-Path $fixture 'czero/dlls/mp.dll'),'fixture')
$original="game `"Condition Zero`"`r`ngamedll `"dlls/mp.dll`"`r`n"
$plugins="// other plugin configuration`r`n"
$libPath=Join-Path $fixture 'czero/liblist.gam'
$pluginsPath=Join-Path $fixture 'czero/addons/metamod/plugins.ini'
$manifest=Join-Path $fixture 'czero/addons/cz_help/install-state.json'
[IO.File]::WriteAllText($libPath,$original)
[IO.File]::WriteAllText($pluginsPath,$plugins)
$helper=$null
$ini=Join-Path (Split-Path -Parent $binary) 'CZ_Help.ini'
$iniBackup=if(Test-Path -LiteralPath $ini) { [IO.File]::ReadAllBytes($ini) } else { $null }
function Wait-Condition([scriptblock]$Condition,[string]$Message) {
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) { if (& $Condition) { return }; Start-Sleep -Milliseconds 100 }
    throw $Message
}
try {
    foreach ($crash in @($false,$true)) {
        $helper=Start-Process -FilePath $binary -ArgumentList ('--game-path "'+$fixture+'"') -WindowStyle Hidden -PassThru
        Wait-Condition {
            $helper.Refresh()
            if ($helper.HasExited) { throw ('Native helper exited before ready: '+$helper.ExitCode) }
            $panel=[CZNativeSessionTest]::Panel($helper.Id)
            $text=[CZNativeSessionTest]::Text($panel)
            if ($text.Contains([string][char]0x81EA+[char]0x52A8+[char]0x914D+[char]0x7F6E+[char]0x672A+[char]0x5B8C+[char]0x6210)) { throw ('Native setup failed: '+$text) }
            return $text.Contains([string][char]0x51C6+[char]0x5907+[char]0x5C31+[char]0x7EEA)
        } 'Native UI did not display ready'
        if (!(Test-Path -LiteralPath $manifest)) { throw 'Ready was displayed without an install record' }
        if($VerifyModes) {
            $panel=[CZNativeSessionTest]::Panel($helper.Id)
            if($crash -and [CZNativeSessionTest]::SendMessage([CZNativeSessionTest]::GetDlgItem($panel,202),0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 1) { throw 'Mode was not preserved on restart' }
            foreach($removed in 203..206) { if([CZNativeSessionTest]::GetDlgItem($panel,$removed) -ne [IntPtr]::Zero) { throw 'Removed mode radio still exists' } }
            if([CZNativeSessionTest]::Text($panel) -match 'AutoBhopJump|LongJump|MCJ|Double Duck') { throw 'Removed mode label still displayed' }
            if([CZNativeSessionTest]::SendMessage([CZNativeSessionTest]::GetDlgItem($panel,106),0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 0) { throw 'Movement enabled itself on restart' }
            foreach($id in 201..202) {
                $radio=[CZNativeSessionTest]::GetDlgItem($panel,$id)
                if($radio -eq [IntPtr]::Zero) { throw 'Missing jump mode radio' }
                $null=[CZNativeSessionTest]::SendMessage($radio,0xF5,[IntPtr]::Zero,[IntPtr]::Zero)
                foreach($other in 201..202) {
                    $check=[CZNativeSessionTest]::SendMessage([CZNativeSessionTest]::GetDlgItem($panel,$other),0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
                    if($check -ne [int]($other -eq $id)) { throw 'Jump mode radio exclusivity failed' }
                }
            }
        }
        if ($crash) { Stop-Process -InputObject $helper -Force }
        else { $null=[CZNativeSessionTest]::PostMessage([CZNativeSessionTest]::Panel($helper.Id),0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
        if (!$helper.WaitForExit(10000)) { throw 'Test helper did not exit' }
        Wait-Condition { !(Test-Path -LiteralPath $manifest) } 'Native owner exit did not trigger automatic cleanup'
        if ([IO.File]::ReadAllText($libPath) -cne $original -or [IO.File]::ReadAllText($pluginsPath) -cne $plugins -or
            (Test-Path -LiteralPath (Join-Path $fixture 'czero/addons/cz_help/cz_help_mm.dll'))) { throw 'Native cleanup did not restore original files' }
        $helper.Dispose(); $helper=$null
    }
    $resolved=(Resolve-Path -LiteralPath $fixture).Path
    if (!$resolved.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture cleanup boundary mismatch' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
    Write-Output 'PASS: real EXE auto-preparation, ready UI, normal close restoration and crash restoration; original files preserved.'
    if($VerifyModes) { Write-Output 'PASS: exactly two radio buttons are mutually exclusive; selected mode persists, movement stays off on restart.' }
} finally {
    if ($helper) { $helper.Refresh(); if (!$helper.HasExited) { Stop-Process -InputObject $helper -Force }; $helper.Dispose() }
    if($VerifyModes) {
        if($null -ne $iniBackup) { [IO.File]::WriteAllBytes($ini,$iniBackup) }
        elseif(Test-Path -LiteralPath $ini) { Remove-Item -LiteralPath $ini }
    }
}

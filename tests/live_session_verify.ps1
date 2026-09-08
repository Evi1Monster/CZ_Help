[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GamePath,[Parameter(Mandatory=$true)][string]$BinaryPath)
. (Join-Path $PSScriptRoot '../scripts/Common.ps1')
$root=Get-GameRoot $GamePath
if (@(Get-Process -Name CZ_Help -ErrorAction SilentlyContinue).Count) { throw 'Close existing helper first' }
$binary=(Resolve-Path -LiteralPath $BinaryPath).Path
$state=Get-InstallRecord $root
$expected=@{}
foreach ($relative in @('czero\liblist.gam','czero\addons\metamod\plugins.ini','czero\addons\cz_help\metamod.dll','czero\addons\cz_help\cz_help_mm.dll','czero\config.cfg','czero\dlls\mp.dll')) {
    $file=Join-Path $root $relative
    $expected[$relative]=if(Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash } else { $null }
}
if($state) { foreach($record in $state.Files) { $expected[$record.Relative]=if($null -ne $record.Original) { Get-ByteHash ([Convert]::FromBase64String($record.Original)) } else { $null } } }
function Wait-Condition([scriptblock]$Condition,[string]$Message) {
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while([DateTime]::UtcNow -lt $deadline) { if(& $Condition) { return }; Start-Sleep -Milliseconds 100 }
    throw $Message
}
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
$helper=$null
try {
    $helper=Start-Process -FilePath $binary -ArgumentList ('--game-path "'+$root+'"') -WindowStyle Hidden -PassThru
    Wait-Condition {
        $helper.Refresh(); if($helper.HasExited) { throw 'Helper exited before ready' }
        $text=[CZNativeSessionTest]::Text([CZNativeSessionTest]::Panel($helper.Id))
        if($text.Contains([string][char]0x81EA+[char]0x52A8+[char]0x914D+[char]0x7F6E+[char]0x672A+[char]0x5B8C+[char]0x6210)) { throw $text }
        $text.Contains([string][char]0x51C6+[char]0x5907+[char]0x5C31+[char]0x7EEA)
    } 'Real installation did not become ready'
    $payload=Get-ReleaseFile (Split-Path -Parent $PSScriptRoot) 'cz_help_mm.dll' 'Release' ''
    if((Get-FileHash -LiteralPath (Join-Path $root 'czero/addons/cz_help/cz_help_mm.dll')).Hash -cne (Get-FileHash -LiteralPath $payload).Hash) { throw 'Installed DLL differs from release build' }
    $null=[CZNativeSessionTest]::PostMessage([CZNativeSessionTest]::Panel($helper.Id),0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    if(!$helper.WaitForExit(10000)) { throw 'Helper did not close' }
    Wait-Condition { !(Test-Path -LiteralPath (Join-Path $root 'czero/addons/cz_help/install-state.json')) } 'Real installation was not cleaned'
    foreach($relative in $expected.Keys) {
        $path=Join-Path $root $relative
        $actual=if(Test-Path -LiteralPath $path) { (Get-FileHash -LiteralPath $path).Hash } else { $null }
        if($actual -cne $expected[$relative]) { throw ('Restoration mismatch: '+$relative) }
    }
    Write-Output ('PASS: real Steam directory auto-prepare and ready UI; release DLL matched; '+$expected.Count+' original/config files restored or preserved; game was never launched.')
} finally { if($helper) { $helper.Refresh(); if(!$helper.HasExited) { $null=[CZNativeSessionTest]::PostMessage([CZNativeSessionTest]::Panel($helper.Id),0x10,[IntPtr]::Zero,[IntPtr]::Zero) }; $helper.Dispose() } }
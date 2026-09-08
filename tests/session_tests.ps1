[CmdletBinding()]
param([ValidateSet('All','Restore','Lifecycle')][string]$Suite = 'All')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $PSScriptRoot ('temp\session test-' + [guid]::NewGuid().ToString('N'))
$processes = [Collections.Generic.List[object]]::new()
$original = "game `"Condition Zero`"`r`ngamedll `"dlls\mp.dll`"`r`n"
$build = Join-Path $fixture 'build'
$session = Join-Path $repo 'scripts\Session.ps1'
$install = Join-Path $repo 'scripts\Install.ps1'
$uninstall = Join-Path $repo 'scripts\Uninstall.ps1'
New-Item -ItemType Directory -Path (Join-Path $build 'Release') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $build 'Release\cz_help_mm.dll'),'session plugin v1')
function Require([bool]$Condition,[string]$Message) { if (!$Condition) { throw ('FAIL: ' + $Message) } }
function New-Game([string]$Name) {
    $root = Join-Path $fixture $Name
    New-Item -ItemType Directory -Path (Join-Path $root 'czero\dlls') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $root 'hl.exe'),'fixture')
    [IO.File]::WriteAllText((Join-Path $root 'czero\dlls\mp.dll'),'fixture')
    [IO.File]::WriteAllText((Join-Path $root 'czero\liblist.gam'),$original)
    [IO.File]::WriteAllText((Join-Path $root 'czero\keep.txt'),'unrelated user file')
    return $root
}
function Manifest([string]$Root) { return (Join-Path $Root 'czero\addons\cz_help\install-state.json') }
function Check-Clean([string]$Root) {
    Require (!(Test-Path -LiteralPath (Manifest $Root))) 'manifest remains after successful cleanup'
    Require ([IO.File]::ReadAllText((Join-Path $Root 'czero\liblist.gam')) -ceq $original) 'original liblist bytes were not restored'
    Require (!(Test-Path -LiteralPath (Join-Path $Root 'czero\addons\cz_help\cz_help_mm.dll'))) 'owned plugin remains after cleanup'
    Require ([IO.File]::ReadAllText((Join-Path $Root 'czero\keep.txt')) -ceq 'unrelated user file') 'unrelated file changed'
}
function Stop-TestProcess($Process) {
    if ($Process -and !$Process.HasExited) { $Process.Kill(); $Process.WaitForExit() }
}
function Read-Status([string]$Path) {
    try {
        $stream = [IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        try { $reader = [IO.StreamReader]::new($stream); return $reader.ReadToEnd() }
        finally { if ($reader) { $reader.Dispose() } else { $stream.Dispose() } }
    } catch [IO.IOException] { return '' }
}
function Wait-Status($Worker,[string]$Expected) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $value = Read-Status $Worker.Status
        if (($value -split "`n")[0].Trim() -ceq $Expected) { return }
        if ($value.StartsWith('error') -or $Worker.Process.HasExited) { throw "FAIL: expected $Expected; status $value; worker stderr $(Get-Content -LiteralPath $Worker.Error -Raw -ErrorAction SilentlyContinue)" }
        Start-Sleep -Milliseconds 80
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "FAIL: timed out waiting for $Expected; status $value"
}
function Wait-Done($Worker) {
    Require ($Worker.Process.WaitForExit(15000)) 'guardian did not exit'
    Require ($Worker.Process.ExitCode -eq 0) "guardian exited with $($Worker.Process.ExitCode): $([IO.File]::ReadAllText($Worker.Status))"
}
function Start-Owner {
    $alive = Join-Path $fixture ([guid]::NewGuid().ToString('N') + '.alive')
    [IO.File]::WriteAllText($alive,'running')
    $process = Start-Process -FilePath $sleeper -ArgumentList ('"' + $alive + '"') -WindowStyle Hidden -PassThru
    $processes.Add($process)
    return [pscustomobject]@{ Process=$process; Alive=$alive; StartTime=$process.StartTime.ToUniversalTime().ToFileTimeUtc() }
}
function Start-Worker([string]$Root,$Owner,[long]$StartTime = 0,[switch]$RemoveStatus) {
    $status = Join-Path $fixture ([guid]::NewGuid().ToString('N') + '.status')
    [IO.File]::WriteAllText($status,'')
    if (!$StartTime) { $StartTime = $Owner.StartTime }
    $args = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $session + '" -GamePath "' + $Root + '" -BuildDir "' + $build + '" -OwnerPid ' + $Owner.Process.Id + ' -OwnerStartTime ' + $StartTime + ' -StatusPath "' + $status + '"'
    if ($RemoveStatus) { $args += ' -RemoveStatusOnSuccess' }
    $errorPath = $status + '.stderr'
    $process = Start-Process -FilePath $powershell -ArgumentList $args -WindowStyle Hidden -PassThru -RedirectStandardError $errorPath -RedirectStandardOutput ($status + '.stdout')
    $null = $process.Handle
    $processes.Add($process)
    return [pscustomobject]@{Process=$process;Status=$status;Error=$errorPath}
}
function Start-Game([string]$Root) {
    $exe = Join-Path $Root 'hl.exe'
    Copy-Item -LiteralPath $sleeper -Destination $exe -Force
    $alive = Join-Path $Root 'game.alive'
    [IO.File]::WriteAllText($alive,'running')
    $process = Start-Process -FilePath $exe -ArgumentList ('"' + $alive + '"') -WindowStyle Hidden -PassThru
    $processes.Add($process)
    return $process
}
try {
    if ($Suite -ne 'Lifecycle') {
        $unicodeGame = New-Game ('unicode ' + [string][char]0x6D4B + [char]0x8BD5)
        & $install -GamePath $unicodeGame -BuildDir $build | Out-Null
        & $uninstall -GamePath $unicodeGame | Out-Null
        Check-Clean $unicodeGame
        Write-Output 'PASS: Unicode game directory survives manifest write and restoration'
        # A crash after restoring one record must not strand the remaining owned files.
        $game = New-Game 'partial-restore'
        & $install -GamePath $game -BuildDir $build | Out-Null
        [IO.File]::WriteAllText((Join-Path $game 'czero\liblist.gam'),$original)
        Remove-Item -LiteralPath (Join-Path $game 'czero\addons\cz_help\metamod.dll')
        & $uninstall -GamePath $game | Out-Null
        Check-Clean $game
        # Preflight every record: an unknown later edit prevents earlier records being restored.
        & $install -GamePath $game -BuildDir $build | Out-Null
        $libBefore = [IO.File]::ReadAllText((Join-Path $game 'czero\liblist.gam'))
        $plugin = Join-Path $game 'czero\addons\cz_help\cz_help_mm.dll'
        [IO.File]::AppendAllText($plugin,' user edit')
        $refused = $false
        try { & $uninstall -GamePath $game | Out-Null } catch { $refused = $true }
        Require $refused 'unknown changed plugin was overwritten'
        Require ([IO.File]::ReadAllText((Join-Path $game 'czero\liblist.gam')) -ceq $libBefore) 'restore changed early records before detecting a later conflict'
        Require ([IO.File]::ReadAllText($plugin).EndsWith(' user edit')) 'unknown content was deleted'
        [IO.File]::WriteAllText($plugin,'session plugin v1')
        & $uninstall -GamePath $game | Out-Null
        Check-Clean $game
        Write-Output 'PASS: resumable partial restore, absence, conflict preflight and retry'

        $emptyPlugins = Join-Path $game 'czero\addons\metamod\plugins.ini'
        [IO.File]::WriteAllBytes($emptyPlugins,[byte[]]@())
        & $install -GamePath $game -BuildDir $build | Out-Null
        [IO.File]::WriteAllBytes($emptyPlugins,[byte[]]@())
        & $uninstall -GamePath $game | Out-Null
        Require ((Get-Item -LiteralPath $emptyPlugins).Length -eq 0) 'empty original file cannot be resumed'
        Check-Clean $game
        Write-Output 'PASS: zero-byte original is distinct from original absence'

        & $install -GamePath $game -BuildDir $build | Out-Null
        $manifestPath = Manifest $game
        $complete = [IO.File]::ReadAllText($manifestPath)
        $state = $complete | ConvertFrom-Json
        $state.Files = @($state.Files | Where-Object Relative -NE 'czero\addons\metamod\plugins.ini')
        $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $refused = $false
        try { & $uninstall -GamePath $game | Out-Null } catch { $refused = $true }
        Require $refused 'manifest missing plugins.ini original was accepted'
        Require ([IO.File]::ReadAllText((Join-Path $game 'czero\liblist.gam')) -match 'addons/cz_help/metamod.dll') 'invalid manifest caused mutations'
        [IO.File]::WriteAllText($manifestPath,$complete)
        $state = $complete | ConvertFrom-Json
        $state.Files = @($state.Files | Where-Object Relative -NE 'czero\addons\cz_help\metamod.dll')
        $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $refused = $false
        try { & $uninstall -GamePath $game | Out-Null } catch { $refused = $true }
        Require $refused 'manifest missing loader backup was accepted'
        [IO.File]::WriteAllText($manifestPath,$complete)
        & $uninstall -GamePath $game | Out-Null
        Check-Clean $game
        Write-Output 'PASS: incomplete manifests rejected before mutation'

        # Update may stop between DLL replacement and final manifest replacement.
        & $install -GamePath $game -BuildDir $build | Out-Null
        $state = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $record = @($state.Files | Where-Object Relative -EQ 'czero\addons\cz_help\cz_help_mm.dll')[0]
        $newBytes = [Text.Encoding]::UTF8.GetBytes('pending update v2')
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $pendingHash = ([BitConverter]::ToString($sha.ComputeHash($newBytes))).Replace('-','') } finally { $sha.Dispose() }
        $record | Add-Member -NotePropertyName PendingHash -NotePropertyValue $pendingHash
        $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        [IO.File]::WriteAllBytes((Join-Path $game $record.Relative),$newBytes)
        & $uninstall -GamePath $game | Out-Null
        Check-Clean $game
        Write-Output 'PASS: interrupted update restores a recorded pending DLL hash'

        . (Join-Path $repo 'scripts\Common.ps1')
        $atomicPath = Join-Path $fixture 'atomic.txt'
        [IO.File]::WriteAllText($atomicPath,'original')
        $lock = [IO.File]::Open($atomicPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        $refused = $false
        try { Set-AtomicFile $atomicPath ([Text.Encoding]::UTF8.GetBytes('replacement')) } catch { $refused = $true } finally { $lock.Dispose() }
        Require $refused 'replace unexpectedly succeeded over a delete-locked target'
        Require ([IO.File]::ReadAllText($atomicPath) -ceq 'original') 'failed atomic replacement damaged original bytes'
        Set-AtomicFile $atomicPath ([Text.Encoding]::UTF8.GetBytes('replacement'))
        Require ([IO.File]::ReadAllText($atomicPath) -ceq 'replacement') 'successful atomic replacement did not replace bytes'
        Require (@(Get-ChildItem -LiteralPath $fixture -Filter '*.CZHelp.*.tmp').Count -eq 0) 'atomic helper left temporary files after returning'
        Write-Output 'PASS: atomic replace failure preserves target and removes temporary files'
    }
    if ($Suite -ne 'Restore') {
        Require (Test-Path -LiteralPath $session) 'automatic prepare/cleanup guardian is missing'
        $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $source = Join-Path $fixture 'sleeper.cs'
        $sleeper = Join-Path $fixture 'owner.exe'
        [IO.File]::WriteAllText($source,'using System; using System.IO; using System.Threading; class Sleeper { static void Main(string[] args) { while (File.Exists(args[0])) Thread.Sleep(50); } }')
        & (Join-Path $env:SystemRoot 'Microsoft.NET\Framework\v4.0.30319\csc.exe') /nologo /target:winexe ('/out:' + $sleeper) $source
        Require ($LASTEXITCODE -eq 0) 'test process compile failed'

        $game = New-Game 'normal'
        $owner = Start-Owner
        $worker = Start-Worker $game $owner
        Wait-Status $worker ready
        $statusBytes = [IO.File]::ReadAllBytes($worker.Status)
        Require ($statusBytes[0] -eq 114) 'status file contains BOM or unexpected prefix'
        Require (Test-Path -LiteralPath (Manifest $game)) 'ready without prepared files'
        Remove-Item -LiteralPath $owner.Alive
        Wait-Done $worker
        Check-Clean $game
        Write-Output 'PASS: prepare, UTF-8 atomic status, normal owner exit and exact cleanup'

        # PID alone is insufficient; a mismatched creation time must not adopt an old install.
        & $install -GamePath $game -BuildDir $build | Out-Null
        $owner = Start-Owner
        $worker = Start-Worker $game $owner ($owner.StartTime + 1)
        Wait-Done $worker
        Require (Test-Path -LiteralPath (Manifest $game)) 'mismatched owner touched existing install'
        & $uninstall -GamePath $game | Out-Null
        Stop-TestProcess $owner.Process
        Write-Output 'PASS: exact owner identity required before adoption'

        # A different selected installation must not be blocked by this fake game's name.
        $other = New-Game 'other-path'
        $otherGame = Start-Game $other
        $owner = Start-Owner
        $worker = Start-Worker $game $owner
        Wait-Status $worker ready
        Stop-TestProcess $owner.Process
        Wait-Done $worker
        Check-Clean $game
        Stop-TestProcess $otherGame
        Write-Output 'PASS: exact game path and owner crash cleanup'

        # Active game without the plugin prevents mutation; owner exit cancels untouched wait.
        $gameProcess = Start-Game $game
        $owner = Start-Owner
        $worker = Start-Worker $game $owner
        Wait-Status $worker waiting_game_exit
        Require (!(Test-Path -LiteralPath (Manifest $game))) 'mutated game files while game was running'
        Stop-TestProcess $owner.Process
        Wait-Done $worker
        Stop-TestProcess $gameProcess
        Check-Clean $game
        Write-Output 'PASS: live missing-plugin wait is cancellable without game mutation'

        # Existing same build can be adopted while game runs. Cleanup waits, and a second
        # guardian must not report ready until the first releases the per-game lock.
        & $install -GamePath $game -BuildDir $build | Out-Null
        $gameProcess = Start-Game $game
        $owner = Start-Owner
        $worker = Start-Worker $game $owner
        Wait-Status $worker ready
        Stop-TestProcess $owner.Process
        Wait-Status $worker waiting_game_exit
        $owner2 = Start-Owner
        $worker2 = Start-Worker $game $owner2
        Wait-Status $worker2 waiting_cleanup
        $owner3 = Start-Owner
        $worker3 = Start-Worker $game $owner3
        Wait-Status $worker3 waiting_cleanup
        Stop-TestProcess $owner3.Process
        Wait-Done $worker3
        Require (Test-Path -LiteralPath (Manifest $game)) 'premature cleanup while game held files'
        Stop-TestProcess $gameProcess
        Wait-Done $worker
        Wait-Status $worker2 ready
        Stop-TestProcess $owner2.Process
        Wait-Done $worker2
        Check-Clean $game
        Write-Output 'PASS: live matching install, delayed cleanup and serialized guardians'

        & $install -GamePath $game -BuildDir $build | Out-Null
        [IO.File]::WriteAllText((Join-Path $build 'Release\cz_help_mm.dll'),'session plugin v2')
        $gameProcess = Start-Game $game
        $owner = Start-Owner
        $worker = Start-Worker $game $owner
        Wait-Status $worker waiting_game_exit
        Require ([IO.File]::ReadAllText((Join-Path $game 'czero\addons\cz_help\cz_help_mm.dll')) -ceq 'session plugin v1') 'live mismatch was updated'
        Stop-TestProcess $gameProcess
        Wait-Status $worker ready
        Require ([IO.File]::ReadAllText((Join-Path $game 'czero\addons\cz_help\cz_help_mm.dll')) -ceq 'session plugin v2') 'waiting mismatch never prepared new version'
        Stop-TestProcess $owner.Process
        Wait-Done $worker
        Check-Clean $game
        Write-Output 'PASS: version mismatch waits then prepares and restores original backup'

        # Interrupted earlier restore is recovered before a new guardian prepares files.
        & $install -GamePath $game -BuildDir $build | Out-Null
        [IO.File]::WriteAllText((Join-Path $game 'czero\liblist.gam'),$original)
        $owner = Start-Owner
        $worker = Start-Worker $game $owner -RemoveStatus
        Wait-Status $worker ready
        Stop-TestProcess $owner.Process
        Require ($worker.Process.WaitForExit(15000)) 'recovery guardian did not exit'
        Require ($worker.Process.ExitCode -eq 0) 'recovery guardian failed'
        Require (!(Test-Path -LiteralPath $worker.Status)) 'successful disposable status was not removed'
        Check-Clean $game
        Write-Output 'PASS: interrupted restore recovery and successful status removal'

        # A terminated guardian leaves a tracked install and abandoned mutex. The next
        # valid owner recovers it, preserving even a pre-existing plugin's original bytes.
        $priorPlugin = Join-Path $game 'czero\addons\cz_help\cz_help_mm.dll'
        [IO.File]::WriteAllText($priorPlugin,'user original plugin')
        & $install -GamePath $game -BuildDir $build | Out-Null
        $owner = Start-Owner
        $worker = Start-Worker $game $owner
        Wait-Status $worker ready
        Stop-TestProcess $worker.Process
        Stop-TestProcess $owner.Process
        $owner = Start-Owner
        $worker = Start-Worker $game $owner
        Wait-Status $worker ready
        Stop-TestProcess $owner.Process
        Wait-Done $worker
        Require ([IO.File]::ReadAllText($priorPlugin) -ceq 'user original plugin') 'guardian recovery lost original pre-install DLL'
        Remove-Item -LiteralPath $priorPlugin
        Check-Clean $game
        Write-Output 'PASS: guardian crash recovery and original DLL backup retention'

        $owner = Start-Owner
        $worker = Start-Worker $game $owner
        Wait-Status $worker ready
        $lib = Join-Path $game 'czero\liblist.gam'
        [IO.File]::AppendAllText($lib,'// user edit')
        Stop-TestProcess $owner.Process
        Require ($worker.Process.WaitForExit(15000)) 'conflict guardian did not stop'
        Require ($worker.Process.ExitCode -ne 0) 'conflict cleanup reported success'
        Require ([IO.File]::ReadAllText($worker.Status).StartsWith('error')) 'conflict is not visible in status'
        Require ([IO.File]::ReadAllText($lib).EndsWith('// user edit')) 'conflict overwrote user data'
        Require (Test-Path -LiteralPath (Manifest $game)) 'conflict discarded recovery backup'
        Write-Output 'PASS: cleanup conflict preserves edits and reports error'
    }
} finally {
    foreach ($process in $processes) { Stop-TestProcess $process }
    $resolved = (Resolve-Path -LiteralPath $fixture).Path
    $testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'temp')) + '\'
    if (!$resolved.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture boundary mismatch' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

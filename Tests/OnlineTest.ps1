# Two copies of the game on this machine, one hosting and one joining over
# 127.0.0.1, the computer playing each side through the online path (the
# joiner's orders go to the host as requests). See Docs/design/feat-online.md.
#
#   1. A match: both must finish the same battle, with the same checksum at
#      the same tick, and neither may stop.
#   2. The joiner's game made to differ on purpose (-tmnetdesync): both must
#      stop and say the games are out of sync.
#   3. A match with every neutral camp (-tmcamps=3), the host playing the
#      monsters, called on time at 4 minutes: the same battle on both, as in 1.
#
# No window and no rendering; each copy ends itself. Run by Tests\OnlineTest.bat.

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$Editor = if ($env:UE_ROOT) { "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" } else { 'E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
$Project = "$Root\TacticalMasters.uproject"
$Logs = "$Root\Saved\Logs"
$Common = @('/Game/Maps/Showcase', '-game', '-unattended', '-nullrhi', '-nosound', '-nosplash', '-tmnetbots')

if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
	Write-Host 'The Unreal editor is open. Close it first: it holds the game module the test runs.'
	exit 3
}

function Stop-Copy($Process) {
	if ($Process -and -not $Process.HasExited) {
		# Stop-Process can be refused by a stuck session; WMI's Terminate is not (CLAUDE.md).
		Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)" | Invoke-CimMethod -MethodName Terminate | Out-Null
	}
}

function Run-Match([string]$Name, [int]$Port, [string[]]$JoinExtra, [int]$Seconds, [string[]]$HostExtra = @()) {
	$HostLog = "$Logs\online-$Name-host.log"
	$JoinLog = "$Logs\online-$Name-join.log"
	Remove-Item $HostLog, $JoinLog -ErrorAction SilentlyContinue
	$HostArgs = @("`"$Project`"") + $Common + @("-tmhost=$Port", "-abslog=`"$HostLog`"") + $HostExtra
	$JoinArgs = @("`"$Project`"") + $Common + @("-tmjoin=127.0.0.1:$Port", "-abslog=`"$JoinLog`"") + $JoinExtra
	$Hosting = Start-Process $Editor -ArgumentList $HostArgs -PassThru -WindowStyle Hidden
	# Give the host a moment to start listening before the joiner knocks.
	$Waited = 0
	while ($Waited -lt 120 -and -not ((Test-Path $HostLog) -and (Select-String -Path $HostLog -Pattern 'ONLINE: hosting' -Quiet))) {
		Start-Sleep -Seconds 1
		$Waited++
	}
	$Joining = Start-Process $Editor -ArgumentList $JoinArgs -PassThru -WindowStyle Hidden
	$Hosting.WaitForExit($Seconds * 1000) | Out-Null
	$Joining.WaitForExit(30000) | Out-Null
	$TimedOut = -not $Hosting.HasExited -or -not $Joining.HasExited
	Stop-Copy $Hosting
	Stop-Copy $Joining
	return @{ Host = $HostLog; Join = $JoinLog; TimedOut = $TimedOut }
}

function Lines($Log, $Pattern) {
	if (-not (Test-Path $Log)) { return @() }
	# The comma keeps a single line an array, so [0] is the line, not its first letter.
	return ,@(Select-String -Path $Log -Pattern $Pattern | ForEach-Object { $_.Line })
}

function Check-Match($Match) {
	$Bad = $false
	$HostSum = Lines $Match.Host 'FINAL CHECKSUM'
	$JoinSum = Lines $Match.Join 'FINAL CHECKSUM'
	$Stopped = (Lines $Match.Host 'ONLINE STOPPED') + (Lines $Match.Join 'ONLINE STOPPED')
	$HostEnd = if ($HostSum.Count) { $HostSum[0] -replace '.*FINAL CHECKSUM ', '' } else { '(none)' }
	$JoinEnd = if ($JoinSum.Count) { $JoinSum[0] -replace '.*FINAL CHECKSUM ', '' } else { '(none)' }
	Write-Host "  host:   $HostEnd"
	Write-Host "  joiner: $JoinEnd"
	foreach ($Line in (Lines $Match.Host 'BATTLE OVER') + (Lines $Match.Join 'BATTLE OVER')) { Write-Host ('  ' + ($Line -replace '.*LogTemp: ', '')) }
	if ($Match.TimedOut) { Write-Host '  A COPY DID NOT FINISH IN TIME'; $Bad = $true }
	if ($Stopped.Count) { $Stopped | ForEach-Object { Write-Host ('  ' + ($_ -replace '.*LogTemp: ', '')) }; $Bad = $true }
	if ($HostSum.Count -eq 0 -or $JoinSum.Count -eq 0) { Write-Host '  A COPY NEVER FINISHED THE BATTLE'; $Bad = $true }
	elseif ($HostEnd -ne $JoinEnd) { Write-Host '  THE TWO COPIES ENDED DIFFERENT BATTLES'; $Bad = $true }
	else { Write-Host '  the same battle on both' }
	return $Bad
}

$Failed = $false

Write-Host '=== a match between two copies ==='
$Match = Run-Match 'match' 7791 @() 600
if (Check-Match $Match) { $Failed = $true }

Write-Host ''
Write-Host '=== the joiner''s game made to differ ==='
$Split = Run-Match 'desync' 7792 @('-tmnetdesync') 300
$HostSaw = Lines $Split.Host 'ONLINE STOPPED: Out of sync'
$JoinSaw = Lines $Split.Join 'ONLINE STOPPED: Out of sync'
foreach ($Line in $HostSaw + $JoinSaw) { Write-Host ('  ' + ($Line -replace '.*LogTemp: ', '')) }
if ($HostSaw.Count -eq 0 -or $JoinSaw.Count -eq 0) { Write-Host '  THE SPLIT WAS NOT CAUGHT BY BOTH'; $Failed = $true }
else { Write-Host '  caught by both' }

Write-Host ''
Write-Host '=== a match with neutral camps ==='
$Camps = Run-Match 'camps' 7793 @() 900 @('-tmcamps=3', '-tmmap=riverwatch_fords', '-tmtime=240')
if (Check-Match $Camps) { $Failed = $true }

Write-Host ''
if ($Failed) {
	Write-Host "ONLINE TEST FAILED. Logs: $Logs\online-*.log"
	exit 1
}
Write-Host 'ONLINE TEST PASSED'
exit 0

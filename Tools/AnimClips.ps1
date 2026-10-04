# Films the clips listed in Saved\AnimCatalog\wanted.txt. Run by Tools\AnimClips.bat.
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$Editor = if ($env:UE_ROOT) { "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" } else { 'E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
$Out = "$Root\Saved\AnimCatalog"
$Wanted = "$Out\wanted.txt"
$Log = "$Root\Saved\Logs\agent-anim-clips.log"

if (-not (Test-Path $Wanted)) {
	Write-Host "Nothing to film: $Wanted is not there."
	exit 1
}
New-Item -ItemType Directory -Force $Out | Out-Null
& $Editor "$Root\TacticalMasters.uproject" /Game/Maps/Showcase -game -unattended -RenderOffscreen -NoTextureStreaming -nosound `
	-tmanimcatalog "-tmanimwanted=$Wanted" -stdout -FullStdOutLogOutput > $Log 2>&1
$Done = Select-String -Path $Log -Pattern 'ANIM STUDIO DONE: (\d+)'
if (-not $Done) {
	Write-Host "THE PICKED CLIPS WERE NOT FILMED -- see $Log"
	exit 1
}
Remove-Item $Wanted -ErrorAction SilentlyContinue
Write-Host $Done.Line
exit 0

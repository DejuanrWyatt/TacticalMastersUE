# Films every animation clip the character map uses, a few heroes at a time,
# then merges their catalogues into Saved\AnimCatalog\catalog.json for the class
# creator. Run by Tools\AnimCatalog.bat.
#
# A few at a time because the editor build holds each clip's raw data as well
# as the clip: thirty heroes' worth in one run is more than the machine has.

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$Editor = if ($env:UE_ROOT) { "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" } else { 'E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
$Out = "$Root\Saved\AnimCatalog"
$Log = "$Root\Saved\Logs\agent-anim-catalog.log"
$PerRun = 4

$Map = Get-Content "$Root\Content\Data\CharacterMap\characters.json" -Raw | ConvertFrom-Json
$Sets = @($Map.animations.PSObject.Properties.Name | Sort-Object)
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
Remove-Item $Log -ErrorAction SilentlyContinue

$Filmed = 0
$Clips = @()
$Bodies = @()
for ($i = 0; $i -lt $Sets.Count; $i += $PerRun) {
	$Batch = $Sets[$i..([Math]::Min($i + $PerRun, $Sets.Count) - 1)]
	$Only = $Batch -join ','
	Write-Host "  $Only"
	$Part = "$Root\Saved\Logs\agent-anim-catalog-$($Batch[0]).log"
	& $Editor "$Root\TacticalMasters.uproject" /Game/Maps/Showcase -game -unattended -RenderOffscreen -NoTextureStreaming -nosound `
		-tmanimcatalog "-tmanimonly=$Only" -stdout -FullStdOutLogOutput > $Part 2>&1
	Get-Content $Part | Add-Content $Log
	$Done = Select-String -Path $Part -Pattern 'ANIM STUDIO DONE: (\d+)'
	if (-not $Done) {
		Write-Host "THE ANIMATIONS OF $Only WERE NOT FILMED -- see $Part"
		exit 1
	}
	$Filmed += [int]$Done.Matches[0].Groups[1].Value
	$Catalog = Get-Content "$Out\catalog-$($Batch[0]).json" -Raw | ConvertFrom-Json
	$Clips += @($Catalog.clips)
	$Bodies += @($Catalog.bodies)
	Remove-Item "$Out\catalog-$($Batch[0]).json"
}

# Which body each class wears, as the game reads it. A class in a skin points at
# its hero: only the hero is filmed, and a skin plays the hero's clips.
$Filmed_Bodies = @($Bodies | ForEach-Object { $_.name })
$Classes = [ordered]@{}
foreach ($Entry in $Catalog.classes.PSObject.Properties) {
	$Body = $Entry.Value
	if ($Filmed_Bodies -notcontains $Body -and $Map.bodies.$Body) { $Body = $Map.bodies.$Body.animations }
	$Classes[$Entry.Name] = $Body
}
$Looks = [ordered]@{}
foreach ($Entry in $Catalog.looks.PSObject.Properties) {
	$Body = $Entry.Value
	if ($Filmed_Bodies -notcontains $Body -and $Map.bodies.$Body) { $Body = $Map.bodies.$Body.animations }
	$Looks[$Entry.Name] = $Body
}

$Merged = [ordered]@{
	format = 'tactical-masters-anim-catalog'
	version = 1
	made = (Get-Date).ToUniversalTime().ToString('o')
	frameSize = $Catalog.frameSize
	thumbSize = $Catalog.thumbSize
	fps = $Catalog.fps
	clips = $Clips
	bodies = $Bodies
	looks = $Looks
	classes = $Classes
	default = $Catalog.default
}
[IO.File]::WriteAllText("$Out\catalog.json", ($Merged | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding $false))
# The line the class creator and this script's callers look for.
Write-Host "ANIM STUDIO DONE: $Filmed clips filmed -> $Out"
Add-Content $Log "ANIM STUDIO DONE: $Filmed clips filmed -> $Out"
exit 0

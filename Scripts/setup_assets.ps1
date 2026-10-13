# Restores the Epic content (ThirdPerson template, level prototyping kit, input
# assets, Manny/Quinn mannequins) that is intentionally NOT stored in this repo.
# It is copied from your own Unreal Engine install, which is the licensed source.
#
# Usage:  .\Scripts\setup_assets.ps1 [-Force]
#   $env:UE_ROOT = "D:\UE_5.8"; .\Scripts\setup_assets.ps1   # non-default engine location
param([switch]$Force)
$ErrorActionPreference = "Stop"

$RequiredVersion = "5.8"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Content  = Join-Path (Join-Path $RepoRoot "AdaptiveCompanion") "Content"

$UERoot = $env:UE_ROOT
if (-not $UERoot) {
    $regKey = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$RequiredVersion"
    if (Test-Path $regKey) { $UERoot = (Get-ItemProperty $regKey).InstalledDirectory }
    if (-not $UERoot) { $UERoot = "C:\Program Files\Epic Games\UE_$RequiredVersion" }
}
$Tpl = Join-Path $UERoot "Templates"
$Res = Join-Path $Tpl "TemplateResources\High"

if (-not (Test-Path (Join-Path $Tpl "TP_ThirdPerson\Content"))) {
    Write-Error "Unreal Engine $RequiredVersion not found at: $UERoot`nInstall it via the Epic Games Launcher or set `$env:UE_ROOT."
}

function Copy-Content($Src, $Dst) {
    if (-not (Test-Path $Src)) { Write-Error "Missing $Src" }
    $target = Join-Path $Content $Dst
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    foreach ($item in Get-ChildItem -Path $Src -Recurse -File) {
        $rel  = $item.FullName.Substring($Src.Length).TrimStart('\')
        $dest = Join-Path $target $rel
        if ($Force -or -not (Test-Path $dest)) {
            New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
            Copy-Item $item.FullName $dest -Force
        }
    }
    Write-Host "  -> Content/$Dst"
}

Write-Host "Restoring Epic content from $UERoot"
Copy-Content (Join-Path $Tpl "TP_ThirdPerson\Content\ThirdPerson")         "ThirdPerson"
Copy-Content (Join-Path $Tpl "TP_ThirdPerson\Content\Variant_Combat")      "Variant_Combat"
Copy-Content (Join-Path $Tpl "TP_ThirdPerson\Content\__ExternalActors__")  "__ExternalActors__"
Copy-Content (Join-Path $Tpl "TP_ThirdPerson\Content\__ExternalObjects__") "__ExternalObjects__"
Copy-Content (Join-Path $Res "Input\Content")                              "Input"
Copy-Content (Join-Path $Res "LevelPrototyping\Content")                   "LevelPrototyping"
Copy-Content (Join-Path $Res "Characters\Content\Mannequins")              "Characters\Mannequins"
Write-Host "Done. Open AdaptiveCompanion\AdaptiveCompanion.uproject."

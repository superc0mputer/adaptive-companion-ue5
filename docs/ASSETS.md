# Assets not included in this repository

The repository contains only source code, config, scripts and docs. Everything under
`AdaptiveCompanion/Content/` is git-ignored except folders we author ourselves (allow-listed in
`.gitignore`, stored via Git LFS per `.gitattributes`).

## Epic Games content (restored by script)

`Scripts/setup_assets.sh` (macOS/Linux) or `Scripts/setup_assets.ps1` (Windows) copies from your
Unreal Engine 5.8 install (`Templates/`):

| Content | Source in engine | Target |
|---|---|---|
| Third Person map and blueprints, Combat variant | `TP_ThirdPerson/Content` | `Content/ThirdPerson`, `Content/Variant_Combat`, `Content/__External*__` |
| Input actions and mapping contexts | `TemplateResources/High/Input` | `Content/Input` |
| Level prototyping kit | `TemplateResources/High/LevelPrototyping` | `Content/LevelPrototyping` |
| Manny / Quinn mannequins | `TemplateResources/High/Characters` | `Content/Characters/Mannequins` |
| Door meshes, mirrored to their pre-5.8 path (the Lvl_Combat door still references it) | `TemplateResources/High/LevelPrototyping/Content/Interactable/Door/Meshes` | `Content/LevelPrototyping/Interactable/Door/Assets/Meshes` |

Set `UE_ROOT` if the engine is installed in a non-default location. Existing files are kept, so
re-running the script only fills in what is missing; pass `--force` / `-Force` to overwrite.

## Third-party packs

None. If any are added, list them here (name, author, link, licence) and keep them out of git.

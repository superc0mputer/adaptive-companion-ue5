# Adaptive Companion (UE5)

A telemetry-driven AI companion framework for adaptive player assistance in Unreal Engine 5.
Master's thesis project, TU Munich (Informatics: Games Engineering).

> Work in progress. Demo GIFs, architecture diagram and results will be added here.

## Getting started

Requirements: Unreal Engine **5.8** (Epic Games Launcher), a C++ toolchain
(Xcode on macOS, Visual Studio 2022 on Windows, or Rider), and [Git LFS](https://git-lfs.com).

```bash
git lfs install
git clone https://github.com/superc0mputer/adaptive-companion-ue5.git
cd adaptive-companion-ue5
```

The repo contains only source, config and scripts. Epic's template content (ThirdPerson, level
prototyping kit, input assets, mannequins) is not redistributed; this script copies it from your
UE install into `Content/`:

```bash
# macOS / Linux
Scripts/setup_assets.sh
```

```powershell
# Windows (PowerShell)
.\Scripts\setup_assets.ps1
```

Then open `AdaptiveCompanion/AdaptiveCompanion.uproject` and let Unreal build the modules.
Details: [docs/ASSETS.md](docs/ASSETS.md).

Press Play in `Lvl_ThirdPerson` or `Lvl_Combat`: the companion is spawned next to you and follows
you. `Companion.TriggerIntervention` in the console tests its intervention branch.

## Repository layout

| Path | Contents |
|---|---|
| `AdaptiveCompanion/` | Unreal project (prototype game) |
| `AdaptiveCompanion/Plugins/AdaptiveCompanionFramework/` | Game-independent framework plugin |
| `AdaptiveCompanion/Source/AdaptiveCompanion/Companion/` | Companion character, AI controller and StateTree nodes |
| `AdaptiveCompanion/Content/Companion/` | Our own assets (`BP_Companion`, `ST_Companion`), stored via Git LFS |
| `Scripts/` | Setup scripts |
| `docs/` | Notes and architecture documentation |

The framework lives in its own runtime plugin so it can be reused in other UE5 projects; the
game module depends on it, never the other way round. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

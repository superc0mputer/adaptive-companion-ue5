# Architecture

The project is split into two parts:

- **Adaptive Companion Framework**: a game-independent runtime plugin that can be dropped into
  other UE5 projects.
- **Prototype game**: the `AdaptiveCompanion` game module, which uses the framework and supplies
  everything specific to this game.

```
AdaptiveCompanion/
├── AdaptiveCompanion.uproject           enables the AdaptiveCompanionFramework plugin
├── Plugins/AdaptiveCompanionFramework/  framework (plugin)
│   └── Source/AdaptiveCompanionFramework/
│       ├── Public/                      API used by games
│       └── Private/
└── Source/AdaptiveCompanion/            prototype game (game module)
```

## Dependency rule

```
AdaptiveCompanion (game)  ──depends on──▶  AdaptiveCompanionFramework (plugin)
```

The dependency only goes one way. The plugin never includes a header from the game module and
never lists it in its `Build.cs`. When the framework needs something from the game, it defines
an interface, delegate, gameplay tag or Data Asset, and the game implements or provides it.

## What belongs where

| Plugin (`AdaptiveCompanionFramework`) | Game (`AdaptiveCompanion`) |
|---|---|
| Telemetry collection: generic events and signals (damage taken, position, camera, input) | Calls into the telemetry API from its own characters, weapons, levels |
| Feature extraction: sliding windows, 0–1 normalization | Which signals exist in this game and how they map to telemetry events |
| Assistance-need estimation (combat, navigation, threat, disorientation) with smoothing | Gameplay context: current scenario, objectives, points of interest |
| Intervention selection: utility scoring, cooldowns, repetition penalties | The concrete companion character, its mesh, animations and StateTree |
| Session logging, debug overlay and replay infrastructure | Concrete behaviors (navigation hints, threat call-outs, drawing aggro) |
| Base classes, interfaces, Data Asset types for tuning | Data Asset instances with the tuned values for this game |
| Its own log category `LogAdaptiveCompanionFramework` | The game's log category `LogAdaptiveCompanion` |

Rule of thumb: if a second, different game would need the same code unchanged, it goes in the
plugin. If it names a specific actor, level, asset or rule of this prototype, it stays in the game.

## Plugin module

| | |
|---|---|
| Module | `AdaptiveCompanionFramework` (Runtime, loading phase Default) |
| Engine dependencies | Core, CoreUObject, Engine, GameplayTags, AIModule, StateTreeModule, GameplayStateTreeModule |
| Plugin dependencies | StateTree, GameplayStateTree |
| Log category | `LogAdaptiveCompanionFramework`, exported for use by games |
| Content | Allowed (`CanContainContent`), for framework-owned Data Assets and debug widgets |

Game-specific assets live in `Content/Companion/`, which is allow-listed in `.gitignore`.

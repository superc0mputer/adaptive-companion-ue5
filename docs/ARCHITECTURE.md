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
| Engine dependencies | Core, CoreUObject, Engine, GameplayTags, AIModule, StateTreeModule, GameplayStateTreeModule, DeveloperSettings, Json, JsonUtilities (public); EngineSettings, Projects (private) |
| Plugin dependencies | StateTree, GameplayStateTree |
| Log category | `LogAdaptiveCompanionFramework`, exported for use by games |
| Content | Allowed (`CanContainContent`), for framework-owned Data Assets and debug widgets |
| Stat group | `STATGROUP_AdaptiveCompanion` (`stat AdaptiveCompanion`) for per-frame costs |

Game-specific assets live in `Content/Companion/`, which is allow-listed in `.gitignore`.

### Session logging

| Class | Role |
|---|---|
| `UAdaptiveLogSubsystem` | Game instance subsystem: one JSON Lines file per play session in `Saved/AdaptiveLogs/`, the API every pipeline stage logs through (`LogRecord`, `LogStruct`, Blueprint *Log Record*) |
| `FAdaptiveSessionLogWriter` | Buffers lines in memory and writes them on a background task, in order |
| `UAdaptiveLogLevelSubsystem` | Writes a `level_start` record whenever a level begins play |
| `UAdaptiveLogSettings` | Project Settings > Plugins > Adaptive Companion Logging |
| `AdaptiveLogRecordType` | Names of the record types (`session_start`, `signal`, `state`, ...) |

Record format, file location, Python loading and measured cost: [LOGGING.md](LOGGING.md).

### Debug overlay

| Class | Role |
|---|---|
| `UAdaptiveDebugSubsystem` | Game instance subsystem: stores the values the stages push (`SetBar`, `SetNumber`, `SetText`) and draws them on the debug canvas while `ac.Debug` is 1 |
| `FAdaptiveDebugData` | The values, grouped by section and kept in insertion order |
| `EAdaptiveDebugSection` | One section per pipeline stage: Telemetry, Features, Need, Context, Selection, Behavior |

Commands, sections and how stages write to it: [DEBUG_OVERLAY.md](DEBUG_OVERLAY.md).

## Companion (game module)

The companion lives in the game module under `Source/AdaptiveCompanion/Companion/`, with its
assets in `Content/Companion/`.

| Class / asset | Role |
|---|---|
| `ACompanionCharacter` → `BP_Companion` | The companion pawn. The Blueprint only sets mesh (`SKM_Quinn_Simple`), Anim Blueprint (`ABP_Unarmed`) and StateTree |
| `ACompanionAIController` | Runs the StateTree assigned on the possessed companion (`UStateTreeAIComponent`) and forwards events to it |
| `ST_Companion` | StateTree (schema *StateTree AI Component*) that decides what the companion does |
| `CompanionStateTreeTasks` | C++ StateTree nodes: tasks *Follow Player*, *Reposition Near Player*, *Intervention Placeholder*; conditions *Player Further Than*, *Companion Hidden From Player* |
| `CompanionVisibility` | `IsVisibleToPlayer`: field of view + line of sight test against the player's camera |
| `UCompanionSpawnSubsystem` | Spawns the companion next to the player in every game level that has none |
| `UCompanionNavBoundsSubsystem` | Adds a NavMeshBoundsVolume around all walkable geometry if the level's own volumes don't cover it |
| `UCompanionSettings` | Project Settings > Game > Companion: companion class, spawn offset, navigation bounds |

Logic is in C++ nodes; the StateTree asset only wires them together and holds tunable parameters
(distances, speeds, timeouts), so behavior can be tuned in the editor without recompiling.

### StateTree layout

```
Root                       On Event Companion.Event.Intervention → Intervention
├─ Default
│  ├─ Follow               Follow Player
│  │                         On State Failed → Reposition
│  │                         On State Failed → Follow        (fallback while the companion is visible)
│  │                         On Tick, Player Further Than 2500 cm → Reposition
│  └─ Reposition           Enter condition: Companion Hidden From Player
│                          Reposition Near Player
│                            On State Completed → Follow
└─ Intervention            Intervention Placeholder
                             On State Completed → Default
```

- **Default** is the companion's normal behavior: follow the player at a distance, run when far
  behind. If it gets stuck (wants to move but doesn't for 2 s) or falls more than 25 m behind,
  it teleports to a spot near the player.
- Teleporting only happens out of the player's sight, both where the companion leaves and where it
  appears, the same rule Elizabeth uses in *BioShock Infinite* (she is teleported when she falls
  behind "if it would not be noticeable to the player", GDC postmortem). If every spot is visible,
  the companion keeps following and tries again later.
- **Intervention** is a placeholder branch. Any event with the tag `Companion.Event.Intervention`
  interrupts Default from the Root transition. The pipeline will send these events with a payload
  (#19); until then the console command `Companion.TriggerIntervention` sends one for testing.

### Spawning and navigation in the template levels

`Lvl_ThirdPerson` and `Lvl_Combat` are Epic content restored by `Scripts/setup_assets.*` and not
part of the repository, so nothing is placed in them by hand:

- The companion is spawned at runtime next to the player (`UCompanionSpawnSubsystem`). A
  companion placed in a level takes precedence.
- `Lvl_ThirdPerson` has no navmesh and `Lvl_Combat` only one around the enemy arena.
  `UCompanionNavBoundsSubsystem` adds a volume around everything that blocks pawns, and
  `RuntimeGeneration=Dynamic` (`DefaultEngine.ini`) builds the navmesh at runtime. All AI share
  this navmesh, so enemies in `Lvl_Combat` can now follow the player outside the arena.
- The mannequin Anim Blueprint only plays locomotion while there is acceleration, so the companion
  uses acceleration for path following (`bUseAccelerationForPaths`).

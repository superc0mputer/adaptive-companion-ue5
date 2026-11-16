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
| Feature extraction: sliding windows, aggregations, 0–1 normalization | Which signals exist in this game and how they map to telemetry events; the feature list with its windows and bounds |
| Assistance-need estimation: dimensions, rules, context gates, swappable estimator; smoothing and levels with hysteresis | Gameplay context: current scenario, objectives, points of interest |
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

### Pipeline

| Class | Role |
|---|---|
| `UAdaptivePipelineSubsystem` | Game instance subsystem: runs the six stages in order at a fixed rate (default 10 Hz), takes the game's telemetry (`ReportEvent`, `ReportValue`) and gameplay context (`SetContext`) and hands selected interventions back (`OnIntervention`, `ReportInterventionPhase`) |
| `UAdaptivePipelineStage` | Base class of the stages: `Initialize`, `Update(Frame)`, `Deinitialize` |
| `UAdaptiveTelemetryStage`, `UAdaptiveFeatureStage`, `UAdaptiveNeedStage`, `UAdaptiveContextStage`, `UAdaptiveSelectionStage`, `UAdaptiveBehaviorStage` | The six stages. Features computes the features defined in the config asset; Need scores the need dimensions with its estimator, smooths them and maps them to levels with hysteresis; Selection rates the available actions with the exposé's utility and picks the best or nothing; Behavior hands it to the game and tracks it while it runs; Context holds the contexts the game reports and runs before Need |
| `FAdaptivePipelineFrame` | Data passed through the stages in one update |
| `FAdaptiveFeatureDefinition`, `AdaptiveFeatureMath` | One feature's signal, aggregation, window and normalization; the aggregations and normalization as plain functions |
| `UAdaptiveCompanionAction`, `IAdaptiveCompanionActionHandler` | A companion action as a config asset (relevance per need, contexts, suitability, cost, intrusiveness, cooldown); the interface the game object that carries actions out implements (`CheckAction`, `PerformAction`) |
| `UAdaptiveNeedEstimator`, `UAdaptiveRuleNeedEstimator` | Interface that turns features into one score per need dimension, picked in the config asset; the rule-based implementation (#14) |
| `FAdaptiveNeedDimension`, `FAdaptiveNeedRule`, `FAdaptiveNeedLevelSettings`, `AdaptiveNeedMath` | A dimension's rules and context gates; smoothing half-lives and level thresholds; rule evaluation (weight × min), noisy-OR, smoothing and level transitions as plain functions |
| `UAdaptivePipelineConfig` | Data asset with the framework settings: update rate and the class and parameters of each stage. This game's instance is `Content/Companion/DA_PipelineConfig` |
| `UAdaptivePipelineSettings` | Project Settings > Plugins > Adaptive Companion Pipeline: selects the config asset |
| `AAdaptiveTelemetryTestActor` | Reports test telemetry (`ac.Pipeline.TestActor`) |

Stages, telemetry API, update rate and config: [PIPELINE.md](PIPELINE.md).

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

## Gameplay context (game module)

The game decides what the player is doing and reports it to the pipeline as contexts
([PIPELINE.md](PIPELINE.md#gameplay-context)). The code is in `Source/AdaptiveCompanion/Context/`.

| Class | Role |
|---|---|
| `UGameplayContextSubsystem` | World subsystem, runs in every game level without placing anything. Reports `Context.Combat` while a living `ACombatEnemy` is near the player, `Context.Navigation` otherwise |
| `UGameplayContextSettings` | Project Settings > Game > Gameplay Context: combat radius (15 m), linger time (3 s), check interval (0.1 s) |

## Combat telemetry (game module)

The game reports combat signals to the pipeline ([PIPELINE.md](PIPELINE.md#combat-telemetry)).
Events come from where they happen in the combat template code, the values about nearby enemies
from a subsystem in `Source/AdaptiveCompanion/Telemetry/`.

| Class | Role |
|---|---|
| `ACombatCharacter` | Reports `Combat.DamageTaken`, `Player.Health`, `Combat.AttackStarted`, `Combat.HitDealt` and `Combat.PlayerDied` |
| `ACombatEnemy` | Reports `Combat.EnemyKilled` |
| `UCombatTelemetrySubsystem` | World subsystem, runs in every game level without placing anything. Reports `Combat.EnemiesNearby` and `Combat.NearestEnemyDistance` at the check interval of `UGameplayContextSettings`, within its combat radius |

## Navigation telemetry (game module)

The game reports how the player moves toward the current objective
([PIPELINE.md](PIPELINE.md#navigation-telemetry)). Objectives are in
`Source/AdaptiveCompanion/Navigation/`, the telemetry subsystem in `Source/AdaptiveCompanion/Telemetry/`.

| Class | Role |
|---|---|
| `ANavigationObjective` | Placed in a level: *Order*, *Reach Radius* (3 m), *Show Beacon*, *Phase* (for the scenario log). Only shown while current: a pillar, plus a beacon above the walls if *Show Beacon*. Registers itself with the subsystem on BeginPlay |
| `UNavigationObjectiveSubsystem` | World subsystem. The current objective is the unreached one with the lowest Order; it is shown and reported to the pipeline with `SetObjective`. Reaching it reports `Nav.ObjectiveReached` and broadcasts `OnObjectiveReached` |
| `UNavigationTelemetrySubsystem` | World subsystem, runs in every game level. Reports `Nav.ObjectiveDistance`, `Nav.HeadingAlignment`, `Nav.Speed` at the check interval of `UGameplayContextSettings`, and `Nav.AreaRevisited` |
| `UNavigationTelemetrySettings` | Project Settings > Game > Navigation Telemetry: cell size (5 m), minimum absence for a revisit (5 s), standing-still speed (0.5 m/s) |

## Camera and input telemetry (game module)

The game reports where the player looks and whether they give input
([PIPELINE.md](PIPELINE.md#camera-and-input-telemetry)), from `Source/AdaptiveCompanion/Telemetry/`.

| Class | Role |
|---|---|
| `UCameraInputTelemetrySubsystem` | World subsystem, runs in every game level. Reports `Camera.TurnRate`, `Camera.ObjectiveAngle`, `Combat.EnemiesOutOfView` and `Input.IdleTime` at the check interval of `UGameplayContextSettings`. Reads the player's camera manager (works with any pawn), the framework's current objective and Slate's last user interaction time |

## Test scenarios (game module)

Repeatable situations for testing the framework ([SCENARIOS.md](SCENARIOS.md)). The code is in
`Source/AdaptiveCompanion/Scenario/`, the levels in `Content/Scenarios/`.

| Class / asset | Role |
|---|---|
| `ACombatScenario` | Placed in a level. Runs its waves of enemies in order, restarts a wave when the player dies and writes every phase change to the session log as a `scenario` record. Phases are never reported to the pipeline |
| `ANavigationScenario` | Placed in a level with `ANavigationObjective`s. Logs `Start`, each objective's *Phase* (`started` when it becomes current, `reached`) and `End` |
| `ScenarioLog` | Shared by both: writes the `scenario` record, the overlay line and the output log line |
| `Lvl_CombatScenario` | Walled 30 × 30 m arena with one PlayerStart and an `ACombatScenario` in its center |
| `Lvl_NavigationScenario` | 63 × 63 m maze with four objectives, from an easy corridor to a maze without landmarks |

## Companion (game module)

The companion lives in the game module under `Source/AdaptiveCompanion/Companion/`, with its
assets in `Content/Companion/`.

| Class / asset | Role |
|---|---|
| `ACompanionCharacter` → `BP_Companion` | The companion pawn. The Blueprint only sets mesh (`SKM_Quinn_Simple`), Anim Blueprint (`ABP_Unarmed`), StateTree and the companion's actions |
| `Content/Companion/Actions/DA_Action_*`, `CompanionActions.h` | The companion's actions (`UAdaptiveCompanionAction` assets) and their names ([PIPELINE.md](PIPELINE.md#companion-actions)) |
| `ACompanionAIController` | Runs the StateTree assigned on the possessed companion (`UStateTreeAIComponent`) and forwards events to it. Registers the companion's actions with the pipeline while it controls the companion, answers whether each is possible and starts the selected one's state in the Intervention branch (`IAdaptiveCompanionActionHandler`) |
| `ST_Companion` | StateTree (schema *StateTree AI Component*) that decides what the companion does |
| `CompanionStateTreeTasks` | C++ StateTree nodes: tasks *Follow Player*, *Reposition Near Player*, *Navigation Hint*, *Threat Callout*, *Intervention Placeholder*, *Report Intervention Failed*; conditions *Player Further Than*, *Companion Hidden From Player*, *Is Current Intervention* |
| `ACompanionPathTrail` | The navigation hint's trail of cyan discs on the floor along a path; spawned and destroyed by the *Navigation Hint* task |
| `SCompanionThreatIndicator`, `ACompanionThreatMarker` | The threat call-out's cues: the red arrow at the screen edge (Slate, added to the game viewport) and the red cone above the enemy; created and removed by the *Threat Callout* task |
| `UCompanionStateTreeCommandlet` | `-run=CompanionStateTree`: adds a state per companion action to `ST_Companion`'s Intervention branch and gives it the action's behavior task (editor only) |
| `CompanionVisibility` | `IsVisibleToPlayer`: field of view + line of sight test against the player's camera |
| `UCompanionSpawnSubsystem` | Spawns the companion next to the player in every game level that has none |
| `UCompanionNavBoundsSubsystem` | Adds a NavMeshBoundsVolume around all walkable geometry if the level's own volumes don't cover it |
| `UCompanionSettings` | Project Settings > Game > Companion: companion class, spawn offset, navigation bounds, action checks (path check interval, draw-aggro distance) |

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
└─ Intervention              On State Completed → Default, On State Failed → Default
   ├─ NavigationHint        Enter condition: Is Current Intervention Action.NavigationHint
   │                        Navigation Hint                 (#20)
   ├─ ThreatCallout         Enter condition: Is Current Intervention Action.ThreatCallout
   │                        Threat Callout                  (#21)
   ├─ DrawAggro             Enter condition: Is Current Intervention Action.DrawAggro
   │                        Intervention Placeholder        (until #22)
   └─ UnknownAction         Report Intervention Failed      (catches actions without a state)
```

- **Default** is the companion's normal behavior: follow the player at a distance, run when far
  behind. If it gets stuck (wants to move but doesn't for 2 s) or falls more than 25 m behind,
  it teleports to a spot near the player.
- Teleporting only happens out of the player's sight, both where the companion leaves and where it
  appears, the same rule Elizabeth uses in *BioShock Infinite* (she is teleported when she falls
  behind "if it would not be noticeable to the player", GDC postmortem). If every spot is visible,
  the companion keeps following and tries again later.
- **Intervention** has one child state per companion action (#19). Any event with the tag
  `Companion.Event.Intervention` interrupts Default from the Root transition.
  `ACompanionAIController::PerformAction` (called by the pipeline for the selected action) stores
  the action's name and sends that event; the children are tried in order, and *Is Current
  Intervention* lets in the one for that name. *UnknownAction*, the last child, reports actions
  without a state as `failed`. When a child completes or fails, the branch goes back to Default.
- *NavigationHint* runs the *Navigation Hint* task ([below](#navigation-hint)), *ThreatCallout* the
  *Threat Callout* task ([below](#threat-call-out)). Until its behavior exists (#22), *DrawAggro*
  runs the *Intervention Placeholder* task:
  the companion runs to the player (750 cm/s), shows a text bark above its head ("Hang in there,
  I'm coming!" and the action's name; debug text, so not in Shipping) and reports `started`, then
  `completed` once it arrived and 2 s passed, `failed` if it hasn't arrived after 6 s, or
  `interrupted` if the state is left while it still runs.
- The action states are generated by the commandlet `CompanionStateTree` (editor closed):
  `UnrealEditor-Cmd AdaptiveCompanion.uproject -run=CompanionStateTree`. It adds a state for every
  action on the companion class that doesn't have one yet, with the action's behavior task (or the
  placeholder if it has none yet), keeps *UnknownAction* last, moves the branch's own tasks away,
  compiles and saves `ST_Companion`. A state whose only task is still the placeholder gets the
  behavior task once one exists (#20 and #21 replaced NavigationHint's and ThreatCallout's this way). Any other state is left
  alone, so tasks set up in the editor survive; a new action is an asset plus one run.
- `Companion.TriggerIntervention [Action]` starts an action's state without the pipeline for
  testing.

### Navigation hint

`Action.NavigationHint` (#20) shows the player the way to the current objective, in two ways at once:

- **A trail on the floor.** Cyan discs every 80 cm along the navmesh path from the player, for the
  first 20 m (`ACompanionPathTrail`). They appear one after another from the player outwards
  (20 m/s), so the trail also shows the direction. Cyan stands out from the grey walls and the
  orange objective. The path, not the straight line, because in the maze the right way often
  starts away from the objective.
- **The companion leads.** It runs 10 m ahead along the same path (600 cm/s), stops and turns to
  face the way on, with the bark "This way!" (debug text, like the placeholder's).

The hint ends, and the companion goes back to Default (following), when:

| Ends | Phase | Detail |
|---|---|---|
| The player's path to the objective got 5 m shorter, at least 3 s after the start | `completed` | `player followed` |
| The objective was reached, or another one became current | `completed` | `objective reached` |
| 15 s passed | `completed` | `player didn't follow` |
| No objective, or no full path to it, at the start | `failed` | `no objective` / `no path to the objective` |
| The state was left early (another intervention, the companion lost its pawn) | `interrupted` | |

The trail is removed in every case. A hint the player ignores still counts as `completed`, since
the companion did what it was asked; whether the help *worked* is in the detail, for #23. Every
distance and time is a parameter of the task in `ST_Companion`.

The player's progress is measured as path length, every 0.5 s. The straight-line distance would
miss progress along a detour and count walking into a dead end that happens to be closer.

![The trail along the corridor and the companion standing on it, 10 m ahead of the player](images/navigation-hint.png)

### Threat call-out

`Action.ThreatCallout` (#21) warns the player about the enemy they don't see. It picks the nearest
living enemy within the combat radius (15 m) whose center is outside the camera's view, the same
test as `Combat.EnemiesOutOfView` (`UCameraInputTelemetrySubsystem::IsInCameraView`). That is the
threat need's "an enemy close and out of view". Three cues at once:

- **An arrow at the screen edge.** Two pulsing red chevrons just inside the edge, in the enemy's
  direction (forward is up, so an enemy behind the player puts them at the bottom), with
  "Behind you!" (more than 135° from the view direction), "Left!" or "Right!" on a dark box
  (`SCompanionThreatIndicator`). Hidden as soon as the enemy is in view. Plain Slate on the game
  viewport, so unlike the debug-text barks it shows in every build, the study build included.
- **A marker on the enemy.** A red cone above its head (`ACompanionThreatMarker`), so after turning
  the player knows which enemy was meant, even with four in the arena.
- **The companion reacts.** It stops and turns to face the enemy, with the same words as a bark.

| Ends | Phase | Detail |
|---|---|---|
| The enemy is in the camera's view, whether the player turned or the enemy walked in | `completed` | `threat in view` |
| The enemy died or left the combat radius, or the player died | `completed` | `threat gone` |
| 5 s passed | `completed` | `player didn't look` |
| No enemy out of view at the start | `failed` | `no enemy out of view` |
| The state was left early | `interrupted` | |

Arrow and marker are removed in every case. The detail says *in view* rather than *player looked*,
because the frustum test can't tell who moved; the time to `threat in view` is the `duration` in
the log.

**Why an on-screen arrow** and not only the companion: the threat is by definition out of view, and
so is the companion more often than not, so a cue that lives in the world (a bark, a gesture) would
often be missed exactly when it matters. Direction indicators at the screen edge are the established
way to show off-screen threats in third-person games. The companion still turns to the enemy, so
the help stays the companion's.

![The red arrow at the screen edge with "Left!": the enemy is to the left of the player, outside the view](images/threat-callout.png)

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

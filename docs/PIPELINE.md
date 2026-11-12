# Pipeline

The pipeline is the framework's core loop. It runs six stages in order at a fixed rate and is the
API the game reports telemetry through. It is part of the framework plugin
(`Plugins/AdaptiveCompanionFramework/Source/AdaptiveCompanionFramework/*/Pipeline/`).

![Telemetry from the test actor and the console in the debug overlay](images/pipeline-telemetry.png)

## Stages

| # | Stage | Class | Status |
|---|---|---|---|
| 1 | Telemetry | `UAdaptiveTelemetryStage` | Collects what the game reports, logs it, shows it in the overlay |
| 2 | Features | `UAdaptiveFeatureStage` | Aggregates signals over time windows into features in [0, 1], defined in the config asset ([Features](#features)) |
| 3 | Assistance Need | `UAdaptiveNeedStage` | Scores the need dimensions with a swappable estimator, rule-based by default ([Assistance need](#assistance-need)), smooths them and maps them to levels ([Need levels](#need-levels)) |
| 4 | Context | `UAdaptiveContextStage` | Holds the contexts the game reports ([Gameplay context](#gameplay-context)), which gate the need dimensions, and checks which companion actions are possible ([Companion actions](#companion-actions)) |
| 5 | Intervention Selection | `UAdaptiveSelectionStage` | Rates every available action with the exposé's utility and picks the best, or decides to do nothing ([Intervention selection](#intervention-selection)) |
| 6 | Behavior | `UAdaptiveBehaviorStage` | Hands the selected action to its handler, logs every phase with its time, and tracks it as running until it ends ([Carrying out actions](#carrying-out-actions)) |

Every update, `UAdaptivePipelineSubsystem` creates one `FAdaptivePipelineFrame` and passes it
through the stages. Each stage reads what the stages before it wrote and adds its own output.
Context (4) runs before Need (3), because the need dimensions are gated by the active contexts;
it only copies what the game reported, so it doesn't depend on stages 1–3. The update order is
1, 2, 4, 3, 5, 6.

```
Game ──ReportEvent / ReportValue──▶ 1 Telemetry ─▶ 2 Features ─▶ 4 Context ─▶ 3 Need ─▶ 5 Selection ─▶ 6 Behavior
     ──SetContext───────────────────────────────────────▶
                                         └─────────────── FAdaptivePipelineFrame, once per update ──────────────┘
Game ◀──────────────────────────────────────── OnIntervention ───────────────────────────────────────────────┘
     ──ReportInterventionPhase──▶ 6 Behavior
```

All stages derive from `UAdaptivePipelineStage` and override `Initialize`, `Update(Frame)` and
`Deinitialize`. To replace a stage, subclass its class and pick the subclass in the config asset;
the other stages don't change.

## Reporting telemetry

The game reports two kinds of telemetry, both identified by a name it chooses. The framework
uses no game types.

| | Call | Meaning | Example |
|---|---|---|---|
| Event | `ReportEvent(Name, Value = 1)` | Something happened, optionally with a magnitude | `Combat.DamageTaken` with the damage |
| Value | `ReportValue(Name, Value)` | Current value of something continuous | `Player.Health`, `Player.Speed` |

```cpp
#include "Pipeline/AdaptivePipelineSubsystem.h"

if (UAdaptivePipelineSubsystem* Pipeline = UAdaptivePipelineSubsystem::Get(this))
{
	Pipeline->ReportEvent(TEXT("Combat.DamageTaken"), Damage);
	Pipeline->ReportValue(TEXT("Player.Health"), Health / MaxHealth);
}
```

From Blueprint: *Get Game Instance Subsystem (Adaptive Pipeline Subsystem)* → *Report Event* /
*Report Value*. `Get` returns `nullptr` when the pipeline is disabled.

- **Events** are written to the session log immediately, so their timestamp is when they
  happened. The next update passes all events since the previous one to the stages in
  `Frame.Events`.
- **Values** are sampled: only the latest value per name counts, so the game can report them every
  frame. Each update passes all values in `Frame.Values` and logs those that changed since they
  were last logged.

Names use dots for grouping (`Combat.`, `Nav.`, `Camera.`, `Input.`). Which names exist is decided
per signal group in #8–#10: combat in [Combat telemetry](#combat-telemetry), navigation in
[Navigation telemetry](#navigation-telemetry), camera and input in
[Camera and input telemetry](#camera-and-input-telemetry).

## Gameplay context

The game also reports what the player is currently doing, as a set of named contexts. Several
can be active at once (e.g. navigating while under threat), and the names are chosen by the game,
so the framework still uses no game types.

```cpp
Pipeline->SetContext(TEXT("Context.Combat"), true);
Pipeline->SetContext(TEXT("Context.Navigation"), false);
```

From Blueprint: *Set Context* / *Is Context Active*. Calls that don't change anything are
ignored, so the game can report its contexts every frame.

- A change is logged as a `context` record the moment the game reports it, like events.
- Every update passes the active contexts in `Frame.Contexts`, in the order they became active.
  `Frame.bContextChanged` is true in the update where the set differs from the previous update.
- The Need stage gates its dimensions by them ([Assistance need](#assistance-need)). Selection
  doesn't use the context yet; filtering interventions by context comes in #17, #18.

Entering and leaving combat in a `-game` run of `Lvl_Combat` (payloads shortened):

```json
{"t":3.06,"type":"context","payload":{"name":"Context.Navigation","active":true,"contexts":["Context.Navigation"]}}
{"t":10.95,"type":"context","payload":{"name":"Context.Combat","active":true,"contexts":["Context.Navigation","Context.Combat"]}}
{"t":10.95,"type":"context","payload":{"name":"Context.Navigation","active":false,"contexts":["Context.Combat"]}}
{"t":19.22,"type":"context","payload":{"name":"Context.Combat","active":false,"contexts":[]}}
{"t":19.22,"type":"context","payload":{"name":"Context.Navigation","active":true,"contexts":["Context.Navigation"]}}
```

The player was moved next to an enemy at t=10.93 and away from all enemies at t=16.18, so
Combat ended after the 3 s linger time.

**Contexts in this game** (`UGameplayContextSubsystem`, [ARCHITECTURE.md](ARCHITECTURE.md#gameplay-context-game-module)):

| Context | Active while |
|---|---|
| `Context.Combat` | A living enemy is within 15 m of the player, and until none has been for 3 s |
| `Context.Navigation` | The player is not in combat |

The linger time keeps Combat from flickering when an enemy moves along the edge of the radius.
Radius, linger time and check interval are in Project Settings > Game > Gameplay Context. When a
level ends both are deactivated, so no stale context is left during a level change.

**Why a set of names reported by the game:** only the game knows what counts as combat or
navigation in it (enemies, volumes, scripted sequences), and a single exclusive context would lose
overlaps that matter for selection later. Distance with a linger time was chosen over damage
events (misses an approaching enemy that hasn't hit yet) and over the arena's activation volume
(tied to one level's setup; enemies can follow the player out of the arena).

![Overlay in combat (left) and 3 s after leaving it (right)](images/context-overlay.png)

### Objective

The game also tells the framework where the player currently has to go (#12), e.g. for a
navigation hint later (#20). Like contexts, the game pushes it and the framework only stores a
name and a location:

```cpp
Pipeline->SetObjective(TEXT("Objective.2"), Objective->GetActorLocation());
Pipeline->ClearObjective();

FName Name;
FVector Location;
if (Pipeline->GetCurrentObjective(Name, Location)) { ... }
```

From Blueprint: *Set Objective*, *Clear Objective*, *Get Current Objective*. There is one objective
at a time; setting another replaces it. Calls that change neither name nor location (by at least
1 cm) are ignored.

- Every change is logged right away as an `objective` record: `name`, `active` (false when
  cleared), `x`, `y`, `z` in m.
- Every update passes it on in `Frame.Objective` (`NAME_None` without one) and
  `Frame.ObjectiveLocation`.
- The overlay shows it in the Context section, e.g. `Objective  Objective.2 at (-21.0, 21.0) m`.

**In this game** `UNavigationObjectiveSubsystem` reports the current `ANavigationObjective` as
`Objective.<Order>` whenever it changes and clears it when the last one is reached or the level
ends. Pushed rather than queried through an interface for the same reason as contexts: the
framework stays free of game types, and every change ends up in the log.

## Combat telemetry

The signals the game reports about combat (#8). All are raw: they are in game units (damage, count,
m), and turning them into 0–1 features is the job of the Features stage ([Features](#features)).

| Signal | Kind | Reported when | Reported by |
|---|---|---|---|
| `Combat.DamageTaken` | event (damage) | The player takes damage from any source (enemies, lava) | `ACombatCharacter::TakeDamage` |
| `Player.Health` | value (0–1) | Health changes, and full health on (re)spawn | `ACombatCharacter` |
| `Combat.AttackStarted` | event | The player starts a swing: combo start, every further combo step, charged attack | `ACombatCharacter` |
| `Combat.HitDealt` | event (damage) | A player attack hits a living enemy. Dummies, damageable boxes and ragdolls don't count | `ACombatCharacter::DoAttackTrace` |
| `Combat.EnemyKilled` | event | An enemy dies, from any source | `ACombatEnemy::HandleDeath` |
| `Combat.PlayerDied` | event | The player dies | `ACombatCharacter::HandleDeath` |
| `Combat.EnemiesNearby` | value (count) | Number of living enemies within the combat radius (15 m), every 0.1 s | `UCombatTelemetrySubsystem` |
| `Combat.NearestEnemyDistance` | value (m) | Distance to the nearest living enemy, capped at the radius (15 when none is in range), rounded to 0.1 m, every 0.1 s | `UCombatTelemetrySubsystem` |

**Why these:** they cover the four questions the Combat and Threat needs (#14) will ask.
*Is the player under pressure?* Damage taken and health. *Is the player fighting effectively?*
Attacks and hits together give a hit rate. *Is the fight going anywhere?* Kills. *How close is
the threat, even before it hits?* Enemy count and nearest distance. Death is the strongest
struggle signal and is reported on its own, so it doesn't have to be inferred from health.

- **Only hits on enemies:** hitting a training dummy or a box isn't fighting, and would make the
  hit rate look better than the fight goes.
- **Count and distance always reported:** "no enemy near" is information too, and a gap-free
  series is easier to analyze. Distance is capped at the radius instead of using a marker value
  like -1, so it stays monotonic (further = less threat) and normalizes without a special case.
- **Own subsystem:** the two values come from `UCombatTelemetrySubsystem`, separate from
  context detection (`UGameplayContextSubsystem`), so telemetry and context can change
  independently. Both read the radius and check interval from Project Settings > Game >
  Gameplay Context, so "nearby" means the same in both.
- **Rounded distance:** values are logged on every change, so an unrounded distance would add a
  record at every check while anything moves. 0.1 m is finer than any threat rule needs.

From a scripted `-game` run of `Lvl_Combat`: one swing into empty air, then next to an enemy,
swing, kill it, die (payloads shortened, distance records thinned out):

```json
{"t":2.77,"type":"signal","payload":{"name":"Combat.EnemiesNearby","kind":"value","value":0}}
{"t":2.77,"type":"signal","payload":{"name":"Combat.NearestEnemyDistance","kind":"value","value":15}}
{"t":6.84,"type":"signal","payload":{"name":"Combat.AttackStarted","kind":"event","value":1}}
{"t":14.35,"type":"signal","payload":{"name":"Combat.AttackStarted","kind":"event","value":1}}
{"t":14.44,"type":"signal","payload":{"name":"Combat.EnemiesNearby","kind":"value","value":1}}
{"t":14.44,"type":"signal","payload":{"name":"Combat.NearestEnemyDistance","kind":"value","value":1.1}}
{"t":14.88,"type":"signal","payload":{"name":"Combat.HitDealt","kind":"event","value":1}}
{"t":15.04,"type":"signal","payload":{"name":"Combat.NearestEnemyDistance","kind":"value","value":1.7}}
{"t":19.64,"type":"signal","payload":{"name":"Combat.EnemyKilled","kind":"event","value":1}}
{"t":19.75,"type":"signal","payload":{"name":"Combat.EnemiesNearby","kind":"value","value":0}}
{"t":19.84,"type":"signal","payload":{"name":"Combat.DamageTaken","kind":"event","value":100}}
{"t":19.84,"type":"signal","payload":{"name":"Combat.PlayerDied","kind":"event","value":1}}
{"t":19.86,"type":"signal","payload":{"name":"Player.Health","kind":"value","value":0}}
{"t":20.47,"type":"signal","payload":{"name":"Combat.EnemiesNearby","kind":"value","value":2}}
{"t":22.90,"type":"signal","payload":{"name":"Player.Health","kind":"value","value":1}}
{"t":22.98,"type":"signal","payload":{"name":"Combat.EnemiesNearby","kind":"value","value":0}}
```

The swing at 6.84 hits nothing, so there is no `HitDealt`. The hit knocks the enemy back, which
shows up as a growing distance. After the kill the next wave spawns (2 enemies at 20.47), and
after respawning at the start (22.90) no enemy is near.

![All combat signals in the overlay after a kill and a death](images/combat-telemetry.png)

## Navigation telemetry

The signals the game reports about how the player moves toward the current objective (#9). Like
the combat signals they are raw (m, m/s, −1…1); the features are in [Features](#features).

| Signal | Kind | Reported when | Reported by |
|---|---|---|---|
| `Nav.ObjectiveDistance` | value (m) | Length of the navmesh path to the current objective, every 0.1 s. Straight-line distance if no full path is found; 0 without a current objective | `UNavigationTelemetrySubsystem` |
| `Nav.HeadingAlignment` | value (−1…1) | Cosine between the horizontal movement direction and the direction the path to the objective starts in, every 0.1 s. 0 while standing still (below 0.5 m/s) or without an objective | `UNavigationTelemetrySubsystem` |
| `Nav.Speed` | value (m/s) | Horizontal speed of the player, every 0.1 s | `UNavigationTelemetrySubsystem` |
| `Nav.AreaRevisited` | event | The player enters a 5 m grid cell they had been in before and left at least 5 s ago | `UNavigationTelemetrySubsystem` |
| `Nav.ObjectiveReached` | event (Order) | The player gets within the current objective's reach radius (3 m); the next one becomes current | `UNavigationObjectiveSubsystem` |

**Objectives:** `ANavigationObjective` actors placed in a level, reached in ascending *Order*.
`UNavigationObjectiveSubsystem` tracks the current one (the unreached one with the lowest Order),
reports it to the framework ([Objective](#objective)) and broadcasts `OnObjectiveReached`. Only the
current objective is shown: a 3 m pillar, and a 25 m beacon above it if *Show Beacon* is set. A
level without objectives only reports speed and revisits. The navigation test level is
[Lvl_NavigationScenario](SCENARIOS.md#navigation-scenario).

**How progress is measured:** as the decrease of `Nav.ObjectiveDistance` over time. The path
length along the navmesh is used instead of the straight line, because around walls and in dead
ends the straight line says "close" while the player is still far away. The rate itself is a
feature, `Nav.NoProgress` ([Features](#features)).

**Why these:** distance gives progress, alignment tells right away whether the player heads toward
the objective (without waiting for the distance to change), speed shows hesitating and standing
still, revisits show going in circles and backtracking, and reached objectives mark milestones.

- **Alignment against the path, not the straight line:** walking around a wall toward the
  objective is heading the right way. The direction is taken to the first path point at least
  0.5 m away, so a corner right next to the player doesn't make it jump.
- **Revisits on a grid with a minimum absence:** a revisit only counts after the player was out
  of the cell for 5 s, so walking along a cell border doesn't fire events. The visited cells are
  forgotten when an objective is reached, because the way to the next one may lead back through
  known areas.
- **Always reported, rounded:** like the combat values, distance (0.1 m), alignment (0.01) and
  speed (0.1 m/s) are reported at every check and rounded so that standing still logs nothing.
  When the level ends all three are reset to 0. Without a player (respawning) the last values stay.

Cell size, minimum absence and the standing-still speed are in Project Settings > Game >
Navigation Telemetry (`UNavigationTelemetrySettings`); the check interval is the one of Gameplay
Context. The reach radius is set per objective.

Straight to an objective, then wandering back and forth sideways to the next one for 20 s, then
straight to it, from a scripted `-game` run of `Lvl_ThirdPerson` with two objectives 15 m away
(values sampled every 0.1 s and averaged per phase):

| Phase | Duration | Alignment mean (range) | Distance | Revisits |
|---|---|---|---|---|
| Straight to objective 0 | 2.6 s | 0.96 (0.00…1.00) | 15.1 → 3.0 m | 0 |
| Wandering | 20.0 s | 0.08 (−0.92…0.93) | 19.7 → 19.5 m | 12 |
| Straight to objective 1 | 3.6 s | 0.95 (0.13…1.00) | 19.2 → 2.9 m | 0 |

Heading straight, the distance drops by the running speed (≈ 4.7 m/s) and the alignment stays
near 1. Wandering, the distance doesn't change, the alignment swings around 0 and the player
revisits areas every few seconds. Log excerpt (payloads shortened):

```json
{"t":6.41,"type":"signal","payload":{"name":"Nav.HeadingAlignment","kind":"value","value":1}}
{"t":6.52,"type":"signal","payload":{"name":"Nav.ObjectiveDistance","kind":"value","value":14.2}}
{"t":6.52,"type":"signal","payload":{"name":"Nav.Speed","kind":"value","value":5}}
{"t":10.02,"type":"signal","payload":{"name":"Nav.ObjectiveDistance","kind":"value","value":3}}
{"t":10.05,"type":"signal","payload":{"name":"Nav.ObjectiveReached","kind":"event","value":0}}
{"t":10.22,"type":"signal","payload":{"name":"Nav.ObjectiveDistance","kind":"value","value":19.7}}
{"t":12.54,"type":"signal","payload":{"name":"Nav.HeadingAlignment","kind":"value","value":-0.82}}
```

After objective 0 is reached at 10.05, the distance jumps to the path length to objective 1.

![Heading straight to an objective (left) and wandering sideways to the next one (right)](images/navigation-telemetry.png)

To try it in PIE, place `NavigationObjective` actors in a level (Place Actors, search
"Navigation Objective") and set their *Order*. The grey pillar marks an objective until it is
reached.

## Camera and input telemetry

The signals the game reports about where the player looks and whether they give any input (#10).
Raw like the others (°/s, °, count, s); the features are in [Features](#features).

| Signal | Kind | Reported when | Reported by |
|---|---|---|---|
| `Camera.TurnRate` | value (°/s) | Angle the camera's view direction turned since the last check, divided by the time between checks, rounded to 5 °/s; every 0.1 s | `UCameraInputTelemetrySubsystem` |
| `Camera.ObjectiveAngle` | value (0–180°) | Horizontal angle between the camera's view direction and the direction from the camera to the current objective, rounded to 1°; every 0.1 s. 0 without an objective | `UCameraInputTelemetrySubsystem` |
| `Combat.EnemiesOutOfView` | value (count) | Living enemies within the combat radius (15 m) whose center is outside the camera's view; every 0.1 s | `UCameraInputTelemetrySubsystem` |
| `Input.IdleTime` | value (s) | Whole seconds since the last player input of any kind: keys, mouse buttons, mouse moves, wheel, gamepad beyond its dead zone; every 0.1 s | `UCameraInputTelemetrySubsystem` |

**Why collect them at all:** without them, disorientation can only be inferred from the
navigation signals, and those can't tell "lost" apart from "exploring on purpose". Camera
behavior is an independent and cheap signal for it, and it also tells which threats the player
can see. The choice is reasoned like #8 and #9; #38 checks it against sources.

**Why these:** turn rate shows looking around, the typical behavior of a disoriented player.
Objective angle shows whether the player even looks toward the goal, also while standing still,
where `Nav.HeadingAlignment` is 0. Enemies out of view feed threat awareness (#21) directly
("enemy behind you"). Idle time tells "standing still, thinking or away" apart from "pushing
against a wall", which `Nav.Speed` alone can't.

**Not collected:** move-input magnitude (mostly the same as `Nav.Speed`), per-keypress events and
"erratic input" measures (they would flood the log, mashing already shows in
`Combat.AttackStarted`, and whether they help is for #38).

- **The camera, not the character:** all camera values come from the player's camera manager,
  so they work with every pawn class (`ACombatCharacter` in the combat level, the default
  third-person character in the navigation level) and describe what the player actually sees,
  camera lag included. The objective is the one the game reported to the framework
  ([Objective](#objective)), not a game class.
- **View test without walls:** an enemy counts as in view if its center is inside the camera's
  frustum (horizontal FOV, vertical from the viewport's aspect ratio). Walls aren't traced, so
  an enemy behind a wall in front of the player is still "in view". "Nearby" is the same radius
  and the same "living" rule as `Combat.EnemiesNearby`.
- **Objective angle horizontal only:** looking up or down at a distant objective isn't looking
  away from it. Measured from the camera, because that is where the screen shows it from.
- **Idle time from Slate:** Slate already records the time of the last user interaction for
  every kind of input, so no input binding knows about telemetry. Synthetic mouse moves don't
  count. Whole seconds, because the value grows at every check: rounded to 0.1 s it would log 10
  records per second while the player is idle; this way it logs one.
- **Always reported, rounded:** like the other values all four are reported at every check and
  reset to 0 when the level ends. Without a camera (very first frames) the camera values are 0.
- **Turn rate in world time:** pausing doesn't count as turning slowly. Snapping the camera
  (respawn, scripted turns) shows as one spike of several hundred °/s.

The check interval and combat radius are the ones of Project Settings > Game > Gameplay Context;
the subsystem has no settings of its own.

From scripted `-game` runs that hold the camera still, turn it at 90 °/s, face the target, then
face away from it (target: the first objective in `Lvl_NavigationScenario`, the WarmUp enemy in
`Lvl_CombatScenario`):

| Phase | Navigation level | Combat level |
|---|---|---|
| Still, 4 s | Turn rate 0, objective angle 23–25° | Turn rate 0 (after the camera lag settles) |
| Turning at 90 °/s, 3 s | Turn rate 65–110 °/s, objective angle sweeping 3 → 178 → 117° | Turn rate 80–95 °/s; enemy out of view 0 → 1 → 0 as the camera passes it |
| Facing the target, 3 s | Objective angle 0 | Enemy out of view 0 while the camera follows the circling enemy |
| Facing away, 3 s | Objective angle 180 | Enemy out of view 1 |

`Input.IdleTime` grows by 1 every second in both runs, since the script gives no real input. The
turn rate scatters around 90 °/s because the 0.1 s check doesn't line up exactly with frames; in
the combat level the camera's rotation lag ramps it up and down. Log excerpt from the combat
level, turning away from the enemy (payloads shortened):

```json
{"t":21.64,"type":"signal","payload":{"name":"Camera.TurnRate","kind":"value","value":50}}
{"t":21.76,"type":"signal","payload":{"name":"Camera.TurnRate","kind":"value","value":695}}
{"t":21.76,"type":"signal","payload":{"name":"Combat.EnemiesOutOfView","kind":"value","value":1}}
{"t":21.86,"type":"signal","payload":{"name":"Camera.TurnRate","kind":"value","value":545}}
{"t":22.25,"type":"signal","payload":{"name":"Camera.TurnRate","kind":"value","value":5}}
{"t":22.57,"type":"signal","payload":{"name":"Input.IdleTime","kind":"value","value":26}}
{"t":22.77,"type":"signal","payload":{"name":"Camera.TurnRate","kind":"value","value":0}}
```

**Frame rate:** `-game` runs of `Lvl_CombatScenario` with vsync and frame cap off, frame times
over 20 s, the subsystem disabled and enabled in turn: mean 25.0 ms off, 24.2 and 24.7 ms on,
so no measurable difference. One check takes 9–13 µs on average (timed over 200 checks), at
10 checks per second.

![An enemy behind the player: Combat.EnemiesOutOfView is 1](images/camera-input-telemetry.png)

## Features

The Features stage (#13) turns the raw signals into features in [0, 1], so the Need stage (#14)
can compare and weigh them. Each feature reads one signal, aggregates it over a sliding time
window and maps the result linearly from a configured range to [0, 1]:

```
signal ─▶ samples in the window ─▶ aggregation ─▶ raw value ─▶ [Min, Max] → [0, 1] ─▶ Frame.Features
```

Features are defined in the config asset (*2 Features › Features*), not in code; the framework
knows no signal names. `UAdaptiveFeatureStage`'s built-in list holds only the first loop's
`Combat.DamageRecent`, so a config without a list still drives the naive Need stage.

### Definition

| Field | |
|---|---|
| Name | Name in `Frame.Features`, the log and the overlay |
| Signal | The telemetry event or value it reads |
| Aggregation | See below |
| Denominator | *Ratio* only: the event counted below the line |
| Window | Only samples younger than this count (s). Ignored by *Latest* |
| Threshold | *FractionBelow* / *FractionAbove* only |
| Min, Max | Raw values that map to 0 and 1. Values outside are clamped |
| Invert | Maps Min to 1 and Max to 0 |
| Default | The feature's value while there is no data: nothing reported yet, no denominator events, a *Slope* window less than half full, gate closed |
| Reset Event | Optional event that restarts the window: samples up to and including it no longer count |
| Only While Non Zero | Optional value signal that gates the feature: while it is 0 the feature is *Default*, and its window restarts when the signal becomes non-zero again |

| Aggregation | Reads | Raw value |
|---|---|---|
| Latest | value | The most recent sample |
| Mean | value | Mean of the samples in the window |
| Slope | value | Change per second, least-squares fit over the window |
| FractionBelow / FractionAbove | value | Share of the samples below / above the threshold |
| Count | event | Number of events in the window |
| Sum | event | Sum of the events' values in the window |
| Ratio | events | Number of *Signal* events ÷ number of *Denominator* events in the window |

Events are kept with the time they were reported; values are sampled once per update (10 Hz), so
a window over a value is a series of evenly spaced samples. Each signal's samples are kept as long
as the longest window that reads it, and the newest value sample is always kept for *Latest*. A
definition without a name or signal, a *Ratio* without a denominator or a duplicate name is
skipped with a warning.

### Decisions

**Aggregations.** Each question the needs will ask maps to one aggregation: *how much happened
recently* (count, sum), *how well does it go* (ratio: hits per attack), *what is it now* (latest:
health, enemies), *what is it typically* (mean: turn rate, heading), *is it getting better*
(slope: distance to the objective), *how much of the time* (fraction: standing still). A slope is
fitted by least squares instead of (last − first) / time, because distance and speed are rounded
and noisy, and a single outlier at either end of the window would decide the result. Min, max and
standard deviation are left out: no feature needs them yet, and adding one is a few lines.

**Window lengths.** Per feature, by how fast its situation changes. Combat and threat use short
windows (5–10 s) or the latest value: a fight changes within seconds, and help that comes late is
useless. Navigation and disorientation use 10 s: being lost shows only over time, and a shorter
window would react to a single turn at a junction. Rare events use long windows (revisits 30 s,
deaths 60 s), so one occurrence stays visible long enough to matter. All are first values that the
tuning pass (#16) adjusts on recorded sessions.

**Normalization.** Linear between a fixed Min and Max per feature, clamped, optionally inverted.
Per-player normalization (z-scores or running min/max) was rejected: it needs a warm-up period,
drifts during a session, and makes the same value mean something different for each player, which
breaks the classification tests on scripted traces (H1) and comparisons between study
participants. Fixed bounds are explainable ("3 m/s of progress or more counts as 0") and
reproducible, and the ranges of all signals are known from #8–#10. Features are oriented so that
**1 always means "more of a problem"**, so the Need stage can combine them with positive weights.

**Resets and gates** keep features from reporting artifacts as behavior: when an objective is
reached, the path distance jumps to the next one, which a slope would read as walking away very
fast; without an objective the game reports distance 0, which would read as "no progress".

### Features in this game

Set in `Content/Companion/DA_PipelineConfig`:

| Feature | Signal → aggregation, window | Normalization | 1 means |
|---|---|---|---|
| `Combat.DamageRecent` | `Combat.DamageTaken` → Sum, 5 s | 0–5 | the player's whole health (5 HP) lost in 5 s |
| `Combat.HealthLow` | `Player.Health` → Latest | 0–1, inverted | no health left |
| `Combat.MissRate` | `Combat.HitDealt` ÷ `Combat.AttackStarted`, 10 s | 0–1, inverted; default 0 | no swing hits |
| `Combat.DeathRecent` | `Combat.PlayerDied` → Count, 60 s | 0–1 | died in the last minute |
| `Threat.EnemiesNear` | `Combat.EnemiesNearby` → Latest | 0–4 | 4 or more enemies within 15 m |
| `Threat.Proximity` | `Combat.NearestEnemyDistance` → Latest | 2–15 m, inverted | an enemy within 2 m |
| `Threat.EnemiesUnseen` | `Combat.EnemiesOutOfView` → Latest | 0–2 | 2 or more nearby enemies out of view |
| `Nav.NoProgress` | `Nav.ObjectiveDistance` → Slope, 10 s; reset on `Nav.ObjectiveReached`, only with an objective | −3…0 m/s | the distance doesn't shrink |
| `Nav.Misalignment` | `Nav.HeadingAlignment` → Mean, 10 s; reset and gate as above | −1…1, inverted | moving away from the path |
| `Nav.StandingStill` | `Nav.Speed` → FractionBelow 0.5 m/s, 10 s | 0–1 | standing the whole 10 s |
| `Nav.Revisits` | `Nav.AreaRevisited` → Count, 30 s; reset on `Nav.ObjectiveReached` | 0–4 | 4 revisits in 30 s |
| `Camera.LookingAround` | `Camera.TurnRate` → Mean, 5 s | 0–120 °/s | turning the camera 120 °/s on average |
| `Camera.LookingAway` | `Camera.ObjectiveAngle` → Mean, 10 s; reset and gate as above | 0–180° | looking away from the objective |
| `Input.Idle` | `Input.IdleTime` → Latest | 0–15 s | no input for 15 s or more |

The bounds come from the ranges in #8–#10: the running speed is ≈ 4.7 m/s, so 3 m/s of distance
decrease is clear progress; enemies deal 1 damage to the player's 5 HP; a deliberate look around
turns the camera by 60–120 °/s.

### Logging and overlay

- At session start a `feature_definitions` record lists every definition, so each log says which
  features it was computed with even after the asset changes.
- A `feature` record (`name`, `value`, `raw`) is written when a feature's value, rounded to
  *Log Step* (0.01), changes; `raw` is the value before normalization (`null` without data).
  Windowed means drift with every sample, so without rounding they would log at every update.
  `Frame.Features` keeps the unrounded values.
- The overlay shows every feature as a bar in section *2 Features*, in the order of the asset.

In a 55 s navigation run, the 14 features wrote 949 records, about as many as the signals (872).

### Verification

Scripted `-game` runs of `Lvl_NavigationScenario`: walk along the navmesh path to the current
objective for 15 s (camera turning where the player walks), then backtrack for 20 s (8 s toward the
start, 8 s back, 4 s toward the start), then stand and turn the camera at 60 °/s for 15 s.
Features sampled at 10 Hz and averaged per phase:

| Feature | Straight to objectives | Backtracking | Standing, looking around |
|---|---|---|---|
| `Nav.NoProgress` | 0.03 | 0.86 | 1.00 |
| `Nav.Misalignment` | 0.13 | 0.60 | 0.61 |
| `Nav.StandingStill` | 0.28 | 0.26 | 0.78 (0.60 → 1.00) |
| `Nav.Revisits` | 0 | 0.21 (1 revisit) | 0.20 |
| `Camera.LookingAround` | 0.26 | 0.36 | 0.45 |
| `Camera.LookingAway` | 0.36 | 0.42 | 0.52 |

Going straight, progress is clear and the player heads along the path past two objectives.
Backtracking, the distance
grows and shrinks, the heading points away from the path most of the time, and the player passes
through a cell they had left. Standing still, no progress is 1 and the standing-still share fills
up over the 10 s window. Things the numbers show about the features:

- `Nav.StandingStill` going straight includes the 3 s the player stood before the run started,
  since the window reaches back 10 s.
- `Nav.Misalignment` is 0.5 while standing, because the game reports alignment 0 without
  movement. That is why standing still is its own feature.
- `Camera.LookingAway` is 0.36 even going straight, because in the maze the path leads around
  walls, away from the straight line to the objective.
- `Camera.LookingAround` is above 0 while walking, because the script turns the camera at every
  corner and turnaround, at up to 180 °/s.
- `Input.Idle` is 1 in all runs, since the script gives no real input.

In `Lvl_CombatScenario`, standing with a fixed camera and swinging every 1.5 s while the WarmUp
enemy attacks: `Threat.Proximity` 0.7–1 and `Threat.EnemiesUnseen` 0.5 while the enemy circles
behind the player, `Combat.HealthLow` 0.2 → 0.6 over three hits, `Combat.DamageRecent` up to 0.4
(two hits within 5 s), and `Combat.MissRate` 0.83–1 with one hit in 6–7 swings. Log excerpt at the
first exchange (payloads shortened):

```json
{"t":19.85,"type":"signal","payload":{"name":"Combat.DamageTaken","kind":"event","value":1}}
{"t":19.98,"type":"feature","payload":{"name":"Combat.DamageRecent","value":0.2,"raw":1}}
{"t":19.98,"type":"feature","payload":{"name":"Combat.HealthLow","value":0.2,"raw":0.8}}
{"t":20.04,"type":"signal","payload":{"name":"Combat.HitDealt","kind":"event","value":1}}
{"t":20.11,"type":"feature","payload":{"name":"Combat.MissRate","value":0.86,"raw":0.143}}
```

![Features while standing and looking around in the maze (left) and while an enemy attacks from behind (right)](images/feature-extraction.png)

## Assistance need

The Need stage (#14) turns the features into one score in [0, 1] per assistance-need dimension:
how much help the player needs, and of what kind. The scores come from an **estimator**, an
object inside the stage that is picked in the config asset (*3 Assistance Need › Estimator*). The
rule-based estimator (`UAdaptiveRuleNeedEstimator`) is the default; a machine learning one (#43)
implements the same interface (`UAdaptiveNeedEstimator::Estimate`) and plugs into the same slot.

```
Frame.Features ─▶ rules: weight × min(features) ─▶ noisy-OR per dimension ─▶ context gate ─▶ Frame.Needs
```

### Definition

Dimensions are defined in the config asset (*Estimator › Dimensions*), not in code. The
estimator's built-in list holds only the first loop's `Combat` = `Combat.DamageRecent`, so a config
without rules still drives the naive Selection stage.

| Field | |
|---|---|
| Name | Name in `Frame.Needs`, the log and the overlay |
| Rules | Each rule: optional *Name*, a list of *Features* (each with *Not*, which reads 1 − value) and a *Weight* in [0, 1] |
| Required Contexts | The dimension is evaluated only while all of these contexts are active |
| Requires Objective | The dimension is evaluated only while the game reports an objective |

- A **rule** fires with `Weight × min(feature values)`. A feature the Features stage doesn't
  compute counts as 0 and is warned about once; a rule without features never fires.
- A **dimension** is the noisy-OR of its rules: `1 − Π(1 − rule)`.
- While a gate is closed the dimension is **inactive** and its score is 0.
- The **reason** is the rule that contributes most, by its *Name* or its features
  (`Nav.NoProgress & Nav.Misalignment & not Nav.StandingStill`); empty if no rule fires.
- A dimension without a name, or with the name of an earlier one, is skipped with a warning.

`Frame.Needs` holds an `FAdaptiveNeedState` per dimension: the estimator's `RawScore`, `bActive`
and `Reason`, plus the smoothed `Score` and `Level` from [Need levels](#need-levels).

### Decisions

**Dimensions.** Four kinds of help the companion can give: *combat* (struggling in a fight),
*threat* (not aware of an enemy), *navigation* (not getting closer to the objective) and
*disorientation* (not knowing where to go). They are split so that each one points to a
different intervention, and so the classification test (H1) can tell them apart. Combat vs.
threat separates "losing" from "not seeing it coming"; navigation vs. disorientation separates
"moving the wrong way" from "stopped and searching".

**Rules with min.** A rule is only high while all of its features are high. That is how
ambiguous signals are handled: looking around alone may be exploration, but standing still and
looking around is searching; a high miss rate alone may be swinging at air while approaching, but
missing while taking damage is trading badly. The min (a fuzzy AND) is used instead of a product,
which would shrink quickly with every extra feature (0.7 × 0.7 × 0.7 = 0.34).

**Noisy-OR between rules.** One strong rule is enough on its own (low health while getting hit),
several moderate ones add up, and the score stays in [0, 1] without renormalizing. Rejected:

- *Weighted mean* (the exposé's "configurable combination"): dilutes. Low health would be averaged
  away by the features that are 0, and adding a feature weakens all the others.
- *Max*: ignores several moderate signals together.
- *Clamped sum*: saturates at 1 and loses the order between strong cases.

**Context gates.** A combat need outside combat, or a navigation need without an objective,
means nothing, and some features keep values from before (e.g. `Combat.DeathRecent` for 60 s).
Gating to 0 instead of freezing the last score lets the smoothing ([Need levels](#need-levels)) decay it. The Context
stage therefore runs before the Need stage; it only copies what the game reported.

**Interpretable and swappable.** Every score can be traced to the rule behind it, which the log
and the overlay show. This matters for the thesis (why did the companion help?) and for tuning
(#16). The estimator sits behind an interface so the machine learning estimator (#43) can be
compared on the same features and the same levels.

### Rules in this game

Set in `Content/Companion/DA_PipelineConfig`. The weights are first values; #16 tunes them.

| Dimension | Gate | Rule | Weight |
|---|---|---|---|
| Combat | `Context.Combat` | `HealthLow & DamageRecent`: losing health right now | 1.0 |
| | | `HealthLow` | 0.5 |
| | | `MissRate & DamageRecent`: trading badly | 0.8 |
| | | `DeathRecent` | 0.6 |
| Threat | `Context.Combat` | `EnemiesUnseen & Proximity`: an enemy close and out of view | 1.0 |
| | | `EnemiesUnseen & DamageRecent`: hit by an enemy out of view | 1.0 |
| | | `EnemiesNear`: outnumbered | 0.5 |
| Navigation | `Context.Navigation`, objective | `NoProgress & Misalignment & not StandingStill`: walking the wrong way | 1.0 |
| | | `Revisits & not StandingStill`: going in circles | 0.8 |
| Disorientation | `Context.Navigation`, objective | `StandingStill & LookingAround`: stopped and searching | 1.0 |
| | | `StandingStill & NoProgress & LookingAway`: stopped, facing away from the goal | 0.8 |
| | | `StandingStill & Revisits`: stopped after going in circles | 0.8 |

(Feature names without their `Combat.` / `Threat.` / `Nav.` / `Camera.` prefix.)

- `not StandingStill` keeps the 0.5 that `Misalignment` shows while standing (#13) out of
  navigation.
- Revisits only count as navigation while moving. The 30 s window keeps them for a while after
  the player stops, and a player who went in circles and then stopped is lost, not walking the
  wrong way. Without this, the first scripted run below scored navigation 0.85 and
  disorientation 0.58 while standing and looking around.
- `Input.Idle` is not used: a long idle means "away or thinking", not lost, and every
  disorientation rule already needs the player to be doing something (turning the camera).

### Logging and overlay

- At session start a `need_definitions` record lists the estimator class, the dimensions with
  their rules and gates, and the level settings.
- A `need` record (`need`, `score`, `raw`, `active`, `reason`) is written when a dimension's score
  rounded to *Log Step* (0.01), its reason or whether it is active changed. `state` records mark
  level changes ([Need levels](#need-levels)).
- The overlay shows every dimension as a bar in section *3 Assistance Need*, and below it
  `<dimension> why`: the level and the strongest rule, or *inactive in this context*.

In a 50 s navigation run the four dimensions wrote 298 `need` records (of 2493).

### Verification

The same scripted `-game` runs as for the features (#13). `Lvl_NavigationScenario`, raw scores
(before smoothing, which came later in #15) averaged per phase at 10 Hz:

| Need | Straight to objectives | Backtracking | Standing, looking around |
|---|---|---|---|
| Navigation | 0.03 | **0.64** | 0.35 |
| Disorientation | 0.11 | 0.03 | **0.82** |
| Combat, Threat | inactive | inactive | inactive |

Navigation reached the old single threshold (0.6) 4 s into backtracking (`NoProgress &
Misalignment & not StandingStill`, later `Revisits & not StandingStill`). When the player stops,
disorientation reached it within 2 s and navigation dropped below it 3 s later, as the
standing-still share fills its 10 s window; the 0.35 average is those first seconds. Disorientation while going
straight (up to 0.27) comes from the camera turning at corners and the player standing before the
run starts.

`Lvl_CombatScenario` (standing with a fixed camera, swinging every 1.5 s while the WarmUp enemy
attacks): navigation and disorientation turn inactive when combat starts. Threat is 0.13 while the
enemy is in view (`EnemiesNear`) and 0.56 while it circles behind the player (`EnemiesUnseen &
Proximity`). Combat stays 0 until the first hit, then 0.40 and 0.67 after the second
(`HealthLow & DamageRecent`, above the old threshold, which selected the placeholder intervention), and falls to
0.20 (`HealthLow`) as the hits leave the 5 s window. When the enemy moves along the edge of the
view, threat jumps between 0.13 and 0.74 several times within a second. That flicker is what the
smoothing ([Need levels](#need-levels)) is for.

![Disorientation while standing and looking around in the maze (left), combat need after the second hit (right)](images/need-estimation.png)

## Need levels

The estimator's scores react to every flicker of the features. Before anything acts on them, the
Need stage (#15) smooths each dimension's raw score over time and maps it to a level, **Normal**,
**Elevated** or **High**, so the state doesn't flicker. This happens in the stage, after the
estimator, so it works the same for every estimator (rule-based, #46, #47).

```
raw score ─▶ moving average (rise / fall half-life) ─▶ smoothed score ─▶ enter / exit thresholds ─▶ level
```

### Definition

*3 Assistance Need › Levels* holds the settings for every dimension; *Dimension Levels* overrides
them for single dimensions by name.

| Setting | Default | |
|---|---|---|
| Rise Half Life | 0.5 s | Time until the smoothed score has covered half the way up to a higher raw score. A raw score of 1 reaches the Elevated threshold (0.5) after exactly this time: the reaction time. 0 = no smoothing |
| Fall Half Life | 3 s | The same toward a lower raw score |
| Elevated Enter / Exit | 0.5 / 0.35 | Normal becomes Elevated at or above *Enter*; Elevated becomes Normal below *Exit* |
| High Enter / Exit | 0.75 / 0.6 | Elevated becomes High at or above *Enter*; High becomes Elevated below *Exit* |

- Each update the smoothed score moves toward the raw score by `1 − 0.5^(Δt / half-life)` of the
  remaining distance (an exponential moving average), with the rise or fall half-life depending
  on the direction.
- A level can jump more than one step (Normal straight to High) when the score does.
- Thresholds that contradict each other (an exit above its enter) are capped, so a misconfigured
  asset can't make a level oscillate.
- While a dimension is **inactive** (#14), its level is Normal at once, and its smoothed score
  decays toward 0 at the fall rate.

`FAdaptiveNeedState::Score` is the smoothed score; `RawScore` is the estimator's; `Level` and
`bChanged` (only in the update where the level changed) are what later stages act on.

### Decisions

**Exponential moving average with half-lives.** The exposé's `A = αA + (1 − α)S`, but set by
half-lives in seconds instead of α. A fixed α behaves differently at another update rate; a
half-life gives the same curve at 10 Hz and at 60 Hz (tested), and it reads as a time: "a
full-strength problem is Elevated after 0.5 s".

**Rise faster than fall.** Help should come quickly when a problem appears, but not stop the moment
the player is fine for a second; a fight has pauses between hits. With 0.5 s up and 3 s down, the
combat need after two hits becomes Elevated about 0.6 s later than with the raw score, and holds
for about 5 s after the last hit.

**Hysteresis on top of smoothing.** Each alone isn't enough. Smoothing alone still crosses a single
threshold back and forth when the score hovers around it. Thresholds alone can't stop a raw score
that jumps across both: threat jumped between 0.13 and 0.74 when the enemy was on the edge of the
view (#14). The band between enter and exit absorbs what the smoothing leaves.

**Three levels.** Elevated is "help would be useful", High is "help is urgent". Selection (#18)
can then pick stronger or more intrusive help only for High.

**Inactive means Normal at once.** Combat help after the fight is over makes no sense, so the level
doesn't wait for the score to decay. The score itself does decay, so re-entering combat shortly
afterwards starts from where it was, not from 0.

**Rejected:**

- A minimum time in a level: the fall half-life plus the exit threshold already hold a level for
  several seconds.
- Median filters: they are blind to short real spikes such as a hit.
- A Kalman filter: it needs a noise model we don't have.
- A moving average over a window: the features are already windowed (#13), so a second window
  would add a fixed delay to everything.

The values were chosen by re-smoothing the raw scores logged in the #14 runs offline at 10 Hz:
threat changed level 4–6 times per combat run with the old raw threshold, and once with these
values. A fall half-life of 2 s let threat drop and re-enter within 2 s when the enemy briefly came
into view. All are first values that the tuning pass (#16) adjusts.

### Logging and overlay

- `need_definitions` contains the level settings (`levels`) and overrides (`dimension_levels`).
- The `need` record has the smoothed `score` and the `raw` score; it is written when either,
  rounded to 0.01, its reason or whether it is active changed.
- A `state` record is written when a level changes: `need`, `level`, `previous`, `score`, `raw`,
  `active`, `reason`.
- The overlay bar shows the smoothed score; `<dimension> why` shows the level, the raw score and
  the strongest rule, e.g. `Elevated, raw 0.65, Threat.EnemiesUnseen & Threat.Proximity`.

### Verification

Automation tests: `.Need.Flicker` feeds the alternating 0.13 / 0.74 from #14 for 10 s, and noise of
±0.15 around the Elevated threshold for 30 s; each changes the level at most once, while the same
input without smoothing and hysteresis changes it in nearly every update. `.Need.Reaction` checks
that a lasting raw score of 1 becomes Elevated after one rise half-life and High after two, for
0.5 s and 2 s.

Scripted `-game` runs as in #13 and #14, level changes from the `state` records:

| Run | Raw score ≥ 0.6 (before) | Level changes (now) |
|---|---|---|
| Combat, 90 s, threat | 7 | 3: Elevated when the enemy first gets behind the player (8.7 s), Normal when the player dies and leaves combat, Elevated again in the next fight |
| Combat, 90 s, combat | 5 | 4: Elevated after the third hit, High at the death, Normal on leaving combat, Elevated when the next fight starts with the death still in the 60 s window |
| Navigation, 50 s, navigation | 2 | 4: Elevated 4 s into backtracking, High, Elevated, Normal 10 s after the player stopped |
| Navigation, 50 s, disorientation | 1 | 2: Elevated, then High while standing and looking around |

Navigation now stays Elevated for a while after the player stops, while disorientation rises: the
two overlap for about 6 s. Which one gets help is up to Selection (#18). In the combat run every
placeholder intervention was selected once per Normal → Elevated change (2 in 90 s).

![Smoothed need scores with level, raw score and reason: disorientation High while standing in the maze (left), combat rising behind its raw score while threat holds Elevated (right)](images/stable-state.png)

## Companion actions

The framework knows what the companion can do (#17), but not how. Each action is a config asset
that describes what the selection (#18) needs to know about it; the game registers its actions
together with the object that carries them out, and tells the framework every update whether each
one is possible right now.

```
game: RegisterActions(Handler, [assets]) ─▶ 4 Context, every update:
          contexts allow it?  ─no──▶ unavailable: "needs Context.Combat"
          └ yes ─▶ Handler.CheckAction(asset) ─▶ possible?, reason, current cost ─▶ Frame.Actions
```

### Definition

An action is a `UAdaptiveCompanionAction` data asset:

| Field | | In the utility (#18) |
|---|---|---|
| Action Name | Name in `Frame.Actions`, the log, the overlay and `OnIntervention`, e.g. `Action.NavigationHint` | |
| Description | What it does, for people reading the asset | |
| Relevance | Per need dimension, 0–1: how much the action helps with it. Not listed: 0 | R |
| Required Contexts | Only possible while all of these are active | filter |
| Blocked Contexts | Not possible while any of these is active | filter |
| Suitability | Per context, 0–1: how well it fits. Not listed: 1; with several active contexts the lowest counts | S |
| Base Cost | 0–1, used when the game reports no current cost | C |
| Intrusiveness | 0–1: how much it takes over from the player | I |
| Cooldown | Minimum time between two uses (s) | enforced in #18 |

The game registers its actions with `RegisterActions(Handler, Actions)` and removes them with
`UnregisterActions(Handler)` (C++ and Blueprint). The handler implements
`IAdaptiveCompanionActionHandler::CheckAction(Action)` and returns an `FAdaptiveActionCheck`:
`bPossible`, a `Reason` when not (free of changing numbers, since it is logged on change), and
optionally the current `Cost`.

- Registering the same handler again replaces its actions.
- An empty entry, an action without a name, a name that is already registered, or a handler
  without the interface is skipped with a warning.
- A handler that is destroyed without unregistering (e.g. on a level change) is dropped at the
  next update.

Every update, stage 4 checks each registered action against the active contexts and, only if they
allow it, asks the handler. `Frame.Actions` holds an `FAdaptiveActionState` per action: name,
asset, handler, `bContextAllowed`, `bAvailable`, `Reason`, `Cost` and `Suitability`.

### Decisions

**The exposé's selection (§4.4) decides the fields.** It filters out actions that are unavailable
or inappropriate in the context, then ranks the rest by `U = w_r·R + w_s·S − w_p·P − w_c·C −
w_i·I` within cooldowns and frequency limits. The asset holds everything about an action that
formula needs, so #18 doesn't have to change the assets again. The repetition penalty (P) is the
one value that isn't a property of the action: it comes from the history in #18.

**Hard filter and graded suitability.** The exposé has both a filter and a suitability S in the
utility, so the asset has both: required and blocked contexts remove what makes no sense at all
(a navigation hint during a fight), suitability weighs the rest. In this game the contexts are
exclusive (combat or navigation), so the first actions only need the filter; suitability is there
for games with overlapping contexts.

**Cost from the game.** The exposé's C is a *contextual* cost: drawing an enemy's attention is
cheap when the companion stands next to it and expensive when it has to run across the arena and
leave the player alone. So the game can report a current cost; the asset's value is the default.

**The game registers, and the same object answers.** The actions belong to the companion, not to
the pipeline: another companion could have other actions, and a game with two companions registers
two sets. The object that registers is the one that will carry the actions out in #19, so one
object answers both "can you?" and "do it" and has the companion every check needs.

**Checked in stage 4.** The exposé defines step 4 as "evaluate the current context and available
actions", and the Context stage already runs before Need and Selection. Contexts are checked first
so the game isn't asked about actions that can't apply.

**Rejected:** the action list in `DA_PipelineConfig` (belongs to the companion, and the game should
register its own); one game subclass per action with its own check (spreads the game logic over
many classes); checking availability only when Selection wants to pick something (the overlay and
the log should show it all the time, and at 10 Hz the checks cost microseconds; the one expensive
check, a path search, is throttled by the game).

### Actions in this game

Assets in `Content/Companion/Actions/`, listed on `BP_Companion` (*Companion › Actions*) and
registered by `ACompanionAIController` while it controls the companion. The behaviors themselves
come in #20–#22.

| Action | Relevance | Context | Cost, intrusiveness, cooldown | Possible when |
|---|---|---|---|---|
| `Action.NavigationHint` | Navigation 1.0, Disorientation 0.8 | requires `Context.Navigation` | 0.2, 0.3, 15 s | there is an objective and a full path from the player to it (path search at most once per second and whenever the objective moves) |
| `Action.ThreatCallout` | Threat 1.0 | requires `Context.Combat` | 0.1, 0.2, 8 s | a living enemy within the combat radius is out of the camera's view (the same count as `Combat.EnemiesOutOfView`) |
| `Action.DrawAggro` | Combat 1.0, Threat 0.4 | requires `Context.Combat` | distance to the enemy / 25 m, 0.6, 20 s | a living enemy is near the player and within 25 m of the companion |

Intrusiveness follows how much the action takes over: a call-out only says something, a hint shows
the way but leaves walking to the player, drawing aggro changes the fight. The path check interval
and the 25 m are in Project Settings > Game > Companion › Actions.

### Logging and overlay

- `action_registered`: when the game registers, with the handler class and every action's
  definition (name, asset, relevance, contexts, suitability, base cost, intrusiveness, cooldown).
- `action_availability`: when an action's availability or reason changes, with `name`,
  `available`, `reason`, `cost`, `suitability`.
- `action_unregistered`: `name`, `reason` (`unregistered` or `handler destroyed`).
- Section *4 Context* shows every action, e.g. `available, cost 0.31, suitability 1.00` or
  `unavailable: needs Context.Combat`.

### Verification

Automation tests `.Actions.Definition`, `.Actions.Registration` and `.Actions.Availability` (see
[Testing](#testing)). Scripted `-game` runs as in #13:

- `Lvl_NavigationScenario`: all three registered at the start. The navigation hint is unavailable
  for the first second (*no path to the objective*, the navmesh is still being built at runtime),
  then available for the whole run; the two combat actions are unavailable (*needs
  Context.Combat*). Unregistered when the session ends.
- `Lvl_CombatScenario`: the navigation hint is unavailable (*no objective*, then *needs
  Context.Navigation*). When combat starts (8.4 s) both combat actions become available. The
  threat call-out switches between available and *no enemy out of view* every 1–4 s as the enemy
  circles the player; drawing aggro stays available, its cost 0.31–0.47 as the companion is
  12–8 m from the enemy.

Availability switching often is expected: it reports a fact, not a decision. Not repeating an
action too often is the job of the cooldowns and the repetition penalty in #18.

![Section 4 Context in the maze (left) and in the arena (right)](images/companion-actions.png)

## Intervention selection

Stage 5 (#18) picks the best companion action for the current needs and context, or decides to do
nothing. It rates every action with the utility from the exposé (§4.4) and selects the best one if
it is good enough and nothing speaks against acting now.

```
Frame.Needs (levels, scores) ┐
Frame.Actions (available,    ├─▶ candidates ─▶ U per candidate ─▶ best ≥ MinUtility and allowed now? ─▶ Frame.Intervention
  cost, suitability)         ┘                                    otherwise "do nothing" + reason
```

### Definition

For each action:

```
U = w_r·R + w_s·S − w_p·P − w_c·C − w_i·I·(1 − n)
```

| Term | | From |
|---|---|---|
| R | relevance to a need × that need's smoothed score; only Elevated or High needs count, and the best one is used | action's *Relevance* (#17), need score (#15) |
| n | the score of that need | #15 |
| S | suitability in the active contexts | #17 |
| P | repetition: `0.5^(time since this action was last selected / Repetition Half Life)`, 0 if never | selection history |
| C | current cost | the game (#17) |
| I | intrusiveness, weighed by `1 − n`: the more urgent the need, the less it counts | action (#17) |

An action is a **candidate** if it is available (#17), off its *Cooldown*, and relevant to an
elevated need (R > 0). The best candidate is selected unless one of these holds, in this order;
then the result is "do nothing" with that reason:

1. no need is Elevated or High (*no elevated need*)
2. the previous intervention is still running (*intervention still running*)
3. the last intervention was less than *Min Time Between Interventions* ago (*too soon after the last intervention*)
4. no action is a candidate (*no candidate action*)
5. the best utility is below *Min Utility* (*best utility below 0.50*)

Ties go to the action registered first. The decision is made every update.

| Setting (*5 Intervention Selection*) | Default |
|---|---|
| Weights: Relevance, Suitability, Repetition, Cost, Intrusiveness | 1, 0.3, 0.5, 0.3, 0.4 |
| Min Utility | 0.5 |
| Min Time Between Interventions | 5 s |
| Repetition Half Life | 30 s |
| *6 Behavior* › Max Intervention Duration | 30 s |

**Running interventions.** The Behavior stage marks the dispatched intervention as running until the
game reports `ended`, `completed`, `failed` or `interrupted` for it, or for at most *Max
Intervention Duration*, after which it logs `timed_out`. The pipeline passes it to the stages as
`Frame.RunningIntervention`.

`Frame.Evaluations` holds an `FAdaptiveActionEvaluation` per action (candidate or not, why not,
the need, every term and the utility); `Frame.SelectionReason` says why the result was chosen.

### Decisions

**The exposé's utility, with R tied to the need.** In the exposé, R is "relevance to the estimated
need". Using the action's relevance alone would rate a navigation hint the same at need 0.5 and
0.9, so R is relevance × the need's smoothed score. The levels (#15) decide *when* the companion
may act (a need must be Elevated or High), the score decides *how strongly* an action is pulled.

**Intrusiveness weighed by urgency.** The agency question is whether help takes over too much. A
fixed intrusiveness penalty would either block drawing aggro altogether or make it as easy as a
call-out. Weighing it with `1 − n` keeps intrusive help for when the player clearly struggles:
at combat need 0.5 the intrusiveness of drawing aggro costs 0.12, at 0.9 only 0.02.

**A sum with gates instead of a product.** The common objection to a weighted sum is that one high
term can make up for one that should rule the action out. Here that can't happen: an action is
only a candidate for a need it addresses (R > 0), and actions that don't fit the context are
already unavailable (#17). With that, the sum keeps the exposé's formula and makes each term's
contribution readable in the log, which a product would blur. Rejected: multiplying the terms,
a fixed priority list (can't weigh need strength against intrusiveness), learning which help works
(far too few interventions per session).

**Do nothing as a decision.** "Do nothing" has its own threshold and is logged with its reason, so
the log distinguishes "no need", "help still running", "too soon", "nothing fits" and "nothing
good enough". This is what the evaluation needs to tell missed help from deliberate restraint.

**Every update, not only on a level change.** The first loop only acted when a need became
Elevated. That misses a need that persists (a player lost for half a minute gets one hint) and a
second need while the first is still elevated. Deciding every update, with the cooldown, the
frequency limit and the repetition penalty, lets the companion help again when the need lasts,
but less and less with the same action.

**Defaults.** With them, a navigation hint at navigation need 0.65 rates 0.85; the same hint 15 s
later (when its cooldown ends) only 0.49, so while the need lasts it is repeated about every
16–20 s. Drawing aggro for a threat need of 0.56, through its threat relevance 0.4, rates 0.28 and
is never chosen over the call-out (0.80). Min utility 0.5 is about the utility of a fitting action
for a need that has just become Elevated, minus a fresh repetition penalty. All are first values;
#16 tunes them.

### Logging and overlay

- `selection`: whenever an action is selected, and when "do nothing" or its reason changes, with
  `decision` (action or `none`), `reason` and `candidates`: per action its `utility`, `need` and
  terms (`relevance`, `suitability`, `repetition`, `cost`, `intrusiveness`), or why it was
  `excluded`.
- `intervention` with phase `selected` now carries `need`, its `score` and `level`, and the
  `utility`. A running intervention that times out gets the phase `timed_out`.
- Section *5 Intervention Selection* shows the decision and its reason, every action's utility and
  terms (`U 0.27 for Threat (R 0.44 S 1.00 P 0.78 C 0.10 I 0.20)`) or why it is no candidate
  (`cooldown, 14 s left`), and the last selected action.
- Until the real behaviors exist (#19–#22), every selected action runs the companion's placeholder
  reaction; its bark shows the action's name.

### Verification

Automation tests `.Selection.Utility` (the worked examples above, the intrusiveness weighting),
`.Selection.Nothing` (each reason for doing nothing), `.Selection.Repetition` (a need that stays
elevated for 60 s: hint, cooldown, penalty, hint again at 15.7 s; the frequency limit between two
actions) and `.Selection.Running` (running until the end is reported, other interventions'
ends, timeout).

Scripted `-game` runs as in #13–#17:

- `Lvl_NavigationScenario`, 50 s: the navigation hint is selected when navigation becomes Elevated
  (utility 0.68), again once its cooldown is over while navigation is High (0.66), and a third time
  for disorientation while the player stands and searches (0.63). In between the log shows
  *intervention still running*, *too soon after the last intervention* and *no candidate action*
  (the hint in its cooldown).
- `Lvl_CombatScenario`, 90 s: 6 interventions. The threat call-out when the enemy first gets behind
  the player (0.73); drawing aggro right after two quick hits make combat Elevated (0.90, against
  the call-out, which was unavailable); the call-out during drawing aggro's cooldown; drawing aggro
  again after the respawn, when the next fight starts with the death in the 60 s window.

The game time in these logs runs a little slower than the session time the pipeline uses (the
`-game` runs don't reach their target frame rate), so cooldowns look shorter there.

![Selection in the maze, the hint in its cooldown while it runs (left); in the arena, combat and threat Elevated, drawing aggro just selected and the call-out rated 0.27 because of its repetition penalty (right)](images/intervention-selection.png)

## First loop

The first version of stages 2–6 is deliberately naive (#6): one signal, one score, one
threshold, one reaction, so the whole chain works end to end before each stage is replaced.

```
ACombatCharacter::TakeDamage ─ Combat.DamageTaken ─▶ Combat.DamageRecent ─▶ Combat Elevated ─▶ Intervention.Combat.Placeholder ─▶ companion runs to the player + bark
                               (event, damage)       (sum over 5 s / 5)    (smoothed ≥ 0.5)   (when Combat becomes Elevated)
```

| Stage | What it does now | Parameters (defaults) |
|---|---|---|
| 2 Features | Sums the values of one event over a sliding window, divides by a fixed maximum, clamps to [0, 1]. Since #13 this is one entry of the general [Features](#features) stage | `Combat.DamageRecent`: `Combat.DamageTaken` → Sum, 5 s, 0–5 |
| 3 Need | One dimension whose score is one feature; *Elevated* once its smoothed score reaches the threshold. Since #14 / #15 this is the default of the rule-based [estimator](#assistance-need) with the default [levels](#need-levels) | `Combat` = rule `Combat.DamageRecent`, Elevated from 0.5 (smoothed) |
| 5 Selection | Selected one fixed intervention whenever the need became Elevated. Replaced by the [utility selection](#intervention-selection) in #18, which only picks registered actions | |
| 6 Behavior | Broadcasts the selected intervention on `OnIntervention` | |

With the framework defaults, three hits within 5 s make the Combat need Elevated about 1.3 s later
(raw 0.6, smoothed with a 0.5 s rise half-life). This game's
rules (#14) make it Elevated already at the second hit while health is low, and only in combat.
Since #18 nothing is selected unless the game has registered an action for the need: in this game
that is drawing aggro, which needs an enemy near the player. The parameters are on the stage objects in the config asset, so they can be tuned
without recompiling.

Results are added to `FAdaptivePipelineFrame`: `Features` (name → value), `Needs` (name →
`FAdaptiveNeedState` with `Score`, `Level`, `bChanged`, `RawScore`, `bActive`, `Reason`) and `Intervention`.

### Carrying out actions

Since #19 the framework hands the selected action to the object that registered it, the same
handler that answers `CheckAction` ([Companion actions](#companion-actions)):

```
5 Selection ─ Frame.Intervention ─▶ 6 Behavior: log dispatched ─▶ Handler.PerformAction(asset) ─▶ true: running
                                                                                                └ false: rejected
                                     OnIntervention.Broadcast(name)   (notification for anyone else, e.g. UI)
game: ReportInterventionPhase(name, started) … (completed | failed + reason | interrupted)
```

```cpp
bool AMyController::PerformAction_Implementation(const UAdaptiveCompanionAction* Action)
{
	// start the behavior; false if it can't
	return StartBehavior(Action->ActionName);
}

// later, from the behavior:
Pipeline->ReportInterventionPhase(Name, AdaptiveInterventionPhase::Started);
Pipeline->ReportInterventionPhase(Name, AdaptiveInterventionPhase::Failed, TEXT("didn't reach the player in time"));
```

| Phase | Reported by | |
|---|---|---|
| `selected` | Selection | with need, level, utility |
| `dispatched` | Behavior | handed to the handler |
| `rejected` | Behavior | the handler returned false, or there is none |
| `started` | game | the companion began |
| `completed` | game | finished as intended |
| `failed` | game | couldn't finish; `detail` says why |
| `interrupted` | game | left before it finished, e.g. another event or the companion lost its pawn |
| `timed_out` | Behavior | the game reported no end within *Max Intervention Duration* (30 s) |

The phases are constants in `AdaptiveInterventionPhase`. The final ones (`completed`, `failed`,
`interrupted`, `rejected`, `timed_out`, and `ended` from before #19) end the running intervention,
so Selection can pick the next one, and are logged with `duration`, the seconds since dispatch.
Every phase is logged with its timestamp and shown in section *6 Behavior*, e.g.
`completed at 33.1 s, after 2.3 s` or `failed at 20.0 s: the companion has no state for this action`.

**Why the handler and not `OnIntervention`.** With a broadcast, every listener would have to check
whether the action is its own, and the framework couldn't tell "nobody carried it out" from "it
started". Calling the registering handler gives one owner per action, the same one that said it
was possible, and a refusal that ends up in the log. The broadcast remains for observers.

**In this game** `ACompanionAIController::PerformAction` stores the action's name and sends the
StateTree event that enters the Intervention branch, which picks the action's own state
([ARCHITECTURE.md](ARCHITECTURE.md#statetree-layout)). It returns false while the companion has no
pawn or no running StateTree.

## Update rate

The stages run at *Update Rate* (default 10 Hz), not every frame. Updates are due every
1 / rate seconds of session time. The due time advances by exactly one interval, so the
average rate matches the setting as long as the game runs faster than that. After a hitch, missed
updates are skipped rather than run all at once. While the game is paused no updates run
(*Pause With Game*); events reported meanwhile are kept for the next update.

The cost of an update shows up as *Pipeline Update* under `stat AdaptiveCompanion`, and per stage
in Unreal Insights.

## Config asset

The settings live in a data asset of type `UAdaptivePipelineConfig`. This game uses
`Content/Companion/DA_PipelineConfig`, selected in Project Settings > Plugins > Adaptive
Companion Pipeline (`UAdaptivePipelineSettings`, saved to `DefaultGame.ini`).

| Setting | Default | |
|---|---|---|
| Update Rate | 10 Hz | Stage updates per second (1–120) |
| Pause With Game | true | No updates while the game is paused |
| Stages › 1 Telemetry … 6 Behavior | The stage's base class | Which class runs each stage, with its parameters. An empty slot uses the base class |
| Telemetry › Log Every Update | false | Log every value at every update instead of only on change |
| Features › Features, Log Step | see [Features](#features) | The feature definitions, and the rounding step for logging |
| Need › Estimator, Log Step | see [Assistance need](#assistance-need) | The estimator with its dimensions and rules, the rounding step for logging |
| Need › Levels, Dimension Levels | see [Need levels](#need-levels) | Smoothing half-lives and level thresholds, for all dimensions and per dimension |
| Selection › Weights, Min Utility, Min Time Between Interventions, Repetition Half Life | see [Intervention selection](#intervention-selection) | The utility's weights, the threshold for doing anything, the frequency limit, how fast the repetition penalty fades |
| Behavior › Max Intervention Duration | 30 s | An intervention whose end the game doesn't report counts as finished after this time |

The stage objects in the asset are templates: each session creates its own copies, so stages can
keep state. Without a selected asset (or if it can't be loaded) the pipeline runs with the
defaults and prints a message. *Enabled* in the project settings turns the pipeline off entirely.

## Session log

The pipeline adds these record types to the session log ([LOGGING.md](LOGGING.md)):

| Type | Written | Payload |
|---|---|---|
| `pipeline_start` | At session start | `config` (asset path or `defaults`), `update_rate`, `pause_with_game`, `stages` (class names in update order) |
| `signal` | When an event is reported, when a value changed | `name`, `kind` (`event` or `value`), `value` |
| `feature_definitions` | At session start, after `pipeline_start` | `features`: every feature's definition |
| `feature` | When a feature's value rounded to 0.01 changed | `name`, `value`, `raw` (before normalization, `null` without data) |
| `need_definitions` | At session start, after `feature_definitions` | `estimator` (class), `levels`, `dimension_levels`, and the estimator's definitions (rule-based: `dimensions`) |
| `need` | When a dimension's smoothed or raw score rounded to 0.01, its reason or whether it is active changed | `need`, `score` (smoothed), `raw`, `active`, `reason` |
| `state` | When a need's level changed | `need`, `level` (`Normal`, `Elevated` or `High`), `previous`, `score`, `raw`, `active`, `reason`. A need starts as Normal without a record |
| `context` | When the game activates or deactivates a context | `name`, `active`, `contexts` (all active contexts after the change) |
| `objective` | When the game sets or clears the objective | `name`, `active`, `x`, `y`, `z` (m) |
| `action_registered` | When the game registers actions | `handler` (class), `actions` (every action's definition) |
| `action_availability` | When an action's availability or reason changed | `name`, `available`, `reason`, `cost`, `suitability` |
| `action_unregistered` | When an action is removed | `name`, `reason` (`unregistered` or `handler destroyed`) |
| `selection` | When an action is selected, and when "do nothing" or its reason changes | `decision` (action or `none`), `reason`, `candidates` (per action: `utility`, `need` and the terms, or `excluded`) |
| `intervention` | At every phase of an intervention | `name`, `phase`; `selected` adds `need`, `score`, `level` and `utility`, final phases add `duration` (s since dispatch), `failed` and `rejected` add `detail` ([Carrying out actions](#carrying-out-actions)) |

The phases are `selected` (Selection), `dispatched` (Behavior, right before `OnIntervention`) and
whatever the game reports; the companion reports `started` and `ended`.

```json
{"seq":12,"t":8.28,"game_time":5.40,"type":"signal","payload":{"name":"Combat.DamageTaken","kind":"event","value":25}, ...}
{"seq":19,"t":8.87,"game_time":5.96,"type":"signal","payload":{"name":"Player.Health","kind":"value","value":0.4}, ...}
```

One pass through the loop, from a `-game` run of `Lvl_Combat` (payloads shortened):

```json
{"t":7.135,"type":"signal","payload":{"name":"Combat.DamageTaken","kind":"event","value":1}}   (3x)
{"t":7.251,"type":"feature","payload":{"name":"Combat.DamageRecent","value":0.6}}
{"t":7.251,"type":"state","payload":{"need":"Combat","score":0.6,"level":"Elevated"}}           (before #15; now ~1.3 s later, see Need levels)
{"t":7.251,"type":"intervention","payload":{"name":"Intervention.Combat.Placeholder","phase":"selected", ...}}
{"t":7.251,"type":"intervention","payload":{"name":"Intervention.Combat.Placeholder","phase":"dispatched","receivers":1}}
{"t":7.255,"type":"intervention","payload":{"name":"Intervention.Combat.Placeholder","phase":"started"}}
{"t":9.522,"type":"intervention","payload":{"name":"Intervention.Combat.Placeholder","phase":"ended"}}
{"t":12.210,"type":"state","payload":{"need":"Combat","score":0,"level":"Normal"}}
```

The hit is processed at the next update (here 116 ms later, at most one interval at 10 Hz), and
the companion starts on the next frame.

Values are only logged on change, so for a regular time series forward-fill them:

```python
sig = data[data.type == "signal"]
values = sig[sig.kind == "value"].pivot_table(index="t", columns="name", values="value").ffill()
```

## Debug overlay

Section *1 Telemetry* shows every value with its latest value, and every event with its latest
value and how often it occurred, e.g. `Combat.DamageTaken  10.00 (2x)`. *2 Features* shows
every feature as a bar, *3 Assistance Need* every need dimension's smoothed score as a bar plus its
level, raw score and strongest rule (or *inactive in this context*), *5* the decision with its
reason, every action's utility and terms or why it is no candidate, and the last selected action,
and *6* the latest phase of each intervention with its time, the duration for final phases and the
reason for failures. `ac.Debug.Sections 3 5 6` shows only some sections, e.g. for screenshots.
*4 Context* shows every context the game reported, active or inactive, and since when, and every
registered companion action with its availability.

## Testing

| Console command | |
|---|---|
| `ac.Pipeline.TestActor` | Spawns `AAdaptiveTelemetryTestActor` next to the player, or removes it. It reports `Test.Wave` (a sine between 0 and 1, every frame) and `Test.Pulse` (an event every 2 s) |
| `ac.Pipeline.Value <Name> <Value>` | Reports a value |
| `ac.Pipeline.Event <Name> [Value]` | Reports an event, value 1 by default |
| `ac.Pipeline.Context <Name> <0\|1>` | Activates or deactivates a context |

`ac.Debug 1` then shows the values in the overlay; the session log has the matching `signal`
records.

The Combat need can be raised without fighting: `ac.Pipeline.Context Context.Combat 1`,
`ac.Pipeline.Value Player.Health 0.4`, then `ac.Pipeline.Event Combat.DamageTaken 1` three times
within 5 s (raw score 0.72). An intervention is only selected if an action is available for it,
and drawing aggro needs a real enemy near the player, so this only triggers the companion in the
combat level. `Companion.TriggerIntervention [Action]`
starts an action's state directly, without the pipeline (without a name: the companion's first
action; an unknown name ends in *UnknownAction* and is reported as failed).

Automation tests `AdaptiveCompanion.Pipeline.FixedRate` (update counts at different frame rates,
hitches), `.Telemetry` (event and value hand-off), `.FeatureNeed` (the first loop's feature,
smoothing delay and level), `.Features.Aggregations` (every aggregation, missing data, minimum slope span),
`.Features.Normalize` (range, clamp, invert), `.Features.Stage` (value sampling, windows, reset
events, gates, ratio default, invalid definitions, trimming), `.Need.Rules` (min, negation, weight,
missing features, noisy-OR, labels), `.Need.Estimator` (context and objective gates, strongest rule,
invalid dimensions), `.Need.Smoothing` (half-lives, update-rate independence), `.Need.Levels`
(enter and exit thresholds, jumps, contradicting thresholds), `.Need.Flicker` (alternating and noisy
input), `.Need.Reaction` (time to Elevated and High, per-dimension override), `.Need.Stage` (default
estimator, smoothing, inactive dimensions, no estimator), `.Actions.Definition` (context filter, suitability, relevance), `.Actions.Registration`
(invalid and duplicate actions, handler without the interface, re-registering, unregistering,
destroyed handler), `.Actions.Availability` (context first, the game's answer, reason, cost),
`.Context` (set, change flag,
repeated and no-op calls), `.Objective`, `.Selection.Utility`, `.Selection.Nothing`, `.Selection.Repetition`,
`.Selection.Running` (handler called, final phases, timeout, rejected; see
[Intervention selection](#intervention-selection)) and `.Config`
(default stages):

```bash
UnrealEditor-Cmd AdaptiveCompanion.uproject -ExecCmds="Automation RunTests AdaptiveCompanion.Pipeline; Quit" -unattended -nullrhi
```

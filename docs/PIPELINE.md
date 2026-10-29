# Pipeline

The pipeline is the framework's core loop. It runs six stages in order at a fixed rate and is the
API the game reports telemetry through. It is part of the framework plugin
(`Plugins/AdaptiveCompanionFramework/Source/AdaptiveCompanionFramework/*/Pipeline/`).

![Telemetry from the test actor and the console in the debug overlay](images/pipeline-telemetry.png)

## Stages

| # | Stage | Class | Status |
|---|---|---|---|
| 1 | Telemetry | `UAdaptiveTelemetryStage` | Collects what the game reports, logs it, shows it in the overlay |
| 2 | Features | `UAdaptiveFeatureStage` | Naive: one windowed sum ([first loop](#first-loop)), real features in #13 |
| 3 | Assistance Need | `UAdaptiveNeedStage` | Naive: one dimension, one threshold, real estimation in #14, #15 |
| 4 | Context | `UAdaptiveContextStage` | Holds the contexts the game reports ([Gameplay context](#gameplay-context)), not used by Selection yet |
| 5 | Intervention Selection | `UAdaptiveSelectionStage` | Naive: one fixed intervention, utility selection in #18 |
| 6 | Behavior | `UAdaptiveBehaviorStage` | Hands the intervention to the game and logs its phases, status tracking in #19 |

Every update, `UAdaptivePipelineSubsystem` creates one `FAdaptivePipelineFrame` and passes it
through the stages in this order. Each stage reads what the stages before it wrote and adds its
own output. Context (4) runs before Selection (5), which is the only stage that needs it.

```
Game ──ReportEvent / ReportValue──▶ 1 Telemetry ─▶ 2 Features ─▶ 3 Need ─▶ 4 Context ─▶ 5 Selection ─▶ 6 Behavior
     ──SetContext─────────────────────────────────────────────────────────▶
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
[Navigation telemetry](#navigation-telemetry).

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
- Selection doesn't use the context yet; filtering interventions by context comes in #17, #18.

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
m), and turning them into 0–1 features is the job of the Features stage (#13).

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
the combat signals they are raw (m, m/s, −1…1); features come in #13.

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
feature and is derived in #13.

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

## First loop

The first version of stages 2–6 is deliberately naive (#6): one signal, one score, one
threshold, one reaction, so the whole chain works end to end before each stage is replaced.

```
ACombatCharacter::TakeDamage ─ Combat.DamageTaken ─▶ Combat.DamageRecent ─▶ Combat Elevated ─▶ Intervention.Combat.Placeholder ─▶ companion runs to the player + bark
                               (event, damage)       (sum over 5 s / 5)    (score ≥ 0.6)      (when Combat becomes Elevated)
```

| Stage | What it does now | Parameters (defaults) |
|---|---|---|
| 2 Features | Sums the values of one event over a sliding window, divides by a fixed maximum, clamps to [0, 1] | *Event Name* `Combat.DamageTaken`, *Feature Name* `Combat.DamageRecent`, *Window* 5 s, *Max Value* 5 |
| 3 Need | One dimension whose score is one feature; *Elevated* at or above the threshold, otherwise *Normal* | *Need Name* `Combat`, *Feature Name* `Combat.DamageRecent`, *Threshold* 0.6 |
| 5 Selection | Selects one intervention in the update where the need becomes Elevated; again only after it was Normal | *Need Name* `Combat`, *Intervention Name* `Intervention.Combat.Placeholder` |
| 6 Behavior | Broadcasts the selected intervention on `OnIntervention` | |

With the player's 5 HP and the enemies' 1 damage per hit, three hits within 5 s make the Combat
need Elevated. The parameters are on the stage objects in the config asset, so they can be tuned
without recompiling.

Results are added to `FAdaptivePipelineFrame`: `Features` (name → value), `Needs` (name →
`FAdaptiveNeedState` with `Score`, `bElevated`, `bChanged`) and `Intervention`.

### Receiving interventions

The framework doesn't know what an intervention does. The game listens to `OnIntervention`,
carries it out, and reports its phases back so they end up in the session log:

```cpp
Pipeline->OnIntervention.AddUniqueDynamic(this, &AMyController::HandleIntervention);

void AMyController::HandleIntervention(FName Intervention)
{
	// start the behavior, later:
	Pipeline->ReportInterventionPhase(Intervention, TEXT("started"));
	Pipeline->ReportInterventionPhase(Intervention, TEXT("ended"));
}
```

In this game `ACompanionAIController` listens and moves its StateTree into the Intervention
branch, whose *Intervention Placeholder* task runs to the player, shows a text bark above the
companion and reports `started` and `ended` ([ARCHITECTURE.md](ARCHITECTURE.md#companion-game-module)).
If nothing listens, the Behavior stage prints a warning.

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
| Features, Need, Selection › ... | see [First loop](#first-loop) | Parameters of the naive stages |

The stage objects in the asset are templates: each session creates its own copies, so stages can
keep state. Without a selected asset (or if it can't be loaded) the pipeline runs with the
defaults and prints a message. *Enabled* in the project settings turns the pipeline off entirely.

## Session log

The pipeline adds these record types to the session log ([LOGGING.md](LOGGING.md)):

| Type | Written | Payload |
|---|---|---|
| `pipeline_start` | At session start | `config` (asset path or `defaults`), `update_rate`, `pause_with_game`, `stages` (class names in update order) |
| `signal` | When an event is reported, when a value changed | `name`, `kind` (`event` or `value`), `value` |
| `feature` | When a feature changed | `name`, `value` |
| `state` | When a need's level changed | `need`, `score`, `level` (`Normal` or `Elevated`). A need starts as Normal without a record |
| `context` | When the game activates or deactivates a context | `name`, `active`, `contexts` (all active contexts after the change) |
| `objective` | When the game sets or clears the objective | `name`, `active`, `x`, `y`, `z` (m) |
| `intervention` | At every phase of an intervention | `name`, `phase`; `selected` adds `need` and `score`, `dispatched` adds `receivers` (number of listeners) |

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
{"t":7.251,"type":"state","payload":{"need":"Combat","score":0.6,"level":"Elevated"}}
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
value and how often it occurred, e.g. `Combat.DamageTaken  10.00 (2x)`. *2 Features* and
*3 Assistance Need* show the feature and the Combat score as bars plus the level, *5* the last
selected intervention and *6* the latest phase of each intervention, both with their time.
*4 Context* shows every context the game reported, active or inactive, and since when.

## Testing

| Console command | |
|---|---|
| `ac.Pipeline.TestActor` | Spawns `AAdaptiveTelemetryTestActor` next to the player, or removes it. It reports `Test.Wave` (a sine between 0 and 1, every frame) and `Test.Pulse` (an event every 2 s) |
| `ac.Pipeline.Value <Name> <Value>` | Reports a value |
| `ac.Pipeline.Event <Name> [Value]` | Reports an event, value 1 by default |
| `ac.Pipeline.Context <Name> <0\|1>` | Activates or deactivates a context |

`ac.Debug 1` then shows the values in the overlay; the session log has the matching `signal`
records.

The first loop runs without fighting by reporting three hits:
`ac.Pipeline.Event Combat.DamageTaken 1` three times within 5 s. `Companion.TriggerIntervention`
starts the companion's reaction directly, without the pipeline (as `Intervention.Console`).

Automation tests `AdaptiveCompanion.Pipeline.FixedRate` (update counts at different frame rates,
hitches), `.Telemetry` (event and value hand-off), `.FeatureNeed` (window, normalization,
threshold), `.Context` (set, change flag, repeated and no-op calls), `.Selection` (selected once per
elevation) and `.Config` (default stages):

```bash
UnrealEditor-Cmd AdaptiveCompanion.uproject -ExecCmds="Automation RunTests AdaptiveCompanion.Pipeline; Quit" -unattended -nullrhi
```

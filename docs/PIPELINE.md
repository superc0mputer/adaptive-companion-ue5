# Pipeline

The pipeline is the framework's core loop. It runs six stages in order at a fixed rate and is the
API the game reports telemetry through. It is part of the framework plugin
(`Plugins/AdaptiveCompanionFramework/Source/AdaptiveCompanionFramework/*/Pipeline/`).

![Telemetry from the test actor and the console in the debug overlay](images/pipeline-telemetry.png)

## Stages

| # | Stage | Class | Status |
|---|---|---|---|
| 1 | Telemetry | `UAdaptiveTelemetryStage` | Collects what the game reports, logs it, shows it in the overlay |
| 2 | Features | `UAdaptiveFeatureStage` | Empty, #13 |
| 3 | Assistance Need | `UAdaptiveNeedStage` | Empty, #14, #15 |
| 4 | Context | `UAdaptiveContextStage` | Empty, #7 |
| 5 | Intervention Selection | `UAdaptiveSelectionStage` | Empty, #18 |
| 6 | Behavior | `UAdaptiveBehaviorStage` | Empty, #19 |

Every update, `UAdaptivePipelineSubsystem` creates one `FAdaptivePipelineFrame` and passes it
through the stages in this order. Each stage reads what the stages before it wrote and adds its
own output. Context (4) runs before Selection (5), which is the only stage that needs it.

```
Game ──ReportEvent / ReportValue──▶ 1 Telemetry ─▶ 2 Features ─▶ 3 Need ─▶ 4 Context ─▶ 5 Selection ─▶ 6 Behavior
                                         └─────────────── FAdaptivePipelineFrame, once per update ──────────────┘
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
per signal group in #8–#10.

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

The stage objects in the asset are templates: each session creates its own copies, so stages can
keep state. Without a selected asset (or if it can't be loaded) the pipeline runs with the
defaults and prints a message. *Enabled* in the project settings turns the pipeline off entirely.

## Session log

The pipeline adds two record types to the session log ([LOGGING.md](LOGGING.md)):

| Type | Payload |
|---|---|
| `pipeline_start` | `config` (asset path or `defaults`), `update_rate`, `pause_with_game`, `stages` (class names in update order) |
| `signal` | `name`, `kind` (`event` or `value`), `value` |

```json
{"seq":12,"t":8.28,"game_time":5.40,"type":"signal","payload":{"name":"Combat.DamageTaken","kind":"event","value":25}, ...}
{"seq":19,"t":8.87,"game_time":5.96,"type":"signal","payload":{"name":"Player.Health","kind":"value","value":0.4}, ...}
```

Values are only logged on change, so for a regular time series forward-fill them:

```python
sig = data[data.type == "signal"]
values = sig[sig.kind == "value"].pivot_table(index="t", columns="name", values="value").ffill()
```

## Debug overlay

Section *1 Telemetry* shows every value with its latest value, and every event with its latest
value and how often it occurred, e.g. `Combat.DamageTaken  10.00 (2x)`.

## Testing

| Console command | |
|---|---|
| `ac.Pipeline.TestActor` | Spawns `AAdaptiveTelemetryTestActor` next to the player, or removes it. It reports `Test.Wave` (a sine between 0 and 1, every frame) and `Test.Pulse` (an event every 2 s) |
| `ac.Pipeline.Value <Name> <Value>` | Reports a value |
| `ac.Pipeline.Event <Name> [Value]` | Reports an event, value 1 by default |

`ac.Debug 1` then shows the values in the overlay; the session log has the matching `signal`
records.

Automation tests `AdaptiveCompanion.Pipeline.FixedRate` (update counts at different frame rates,
hitches), `.Telemetry` (event and value hand-off) and `.Config` (default stages):

```bash
UnrealEditor-Cmd AdaptiveCompanion.uproject -ExecCmds="Automation RunTests AdaptiveCompanion.Pipeline; Quit" -unattended -nullrhi
```

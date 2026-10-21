# Debug overlay

The debug overlay shows the pipeline's live values while playing, so values can be tuned by
watching them. It is part of the framework plugin
(`Plugins/AdaptiveCompanionFramework/Source/AdaptiveCompanionFramework/*/Debug/`).

![Debug overlay with demo values](images/debug-overlay.png)

## Showing it

| Console command | |
|---|---|
| `ac.Debug 1` / `ac.Debug 0` | Shows / hides the overlay (off by default) |
| `ac.Debug.Demo` | Toggles animated test values in every section and shows the overlay |
| `ac.Debug.Set <Section> <Name> <Value>` | Sets a test value. Numbers in [0, 1] become bars, other numbers are shown as numbers, everything else as text |
| `ac.Debug.Clear [Section]` | Clears all values, or the values of one section |

`Section` is a section name (`Telemetry`, `Features`, `Need`, `Context`, `Selection`, `Behavior`,
case-insensitive) or its number 1–6, e.g. `ac.Debug.Set Need Need.Combat 0.7`.

From Blueprint: *Set Overlay Visible*, *Is Overlay Visible*.

## Sections

One section per pipeline stage, in pipeline order. A section without values shows *no data*.

| Section | Filled by |
|---|---|
| 1 Telemetry | Raw signals (#5, #8–#10) |
| 2 Features | Windowed, normalized features (now: `Combat.DamageRecent`; all features in #13) |
| 3 Assistance Need | Dimension scores and level (now: `Combat` and `Combat level`; #14, #15) |
| 4 Context | Scenario, gameplay context (#7) |
| 5 Intervention Selection | Last selected intervention and when (candidates with utilities and cooldowns in #18) |
| 6 Behavior | Latest phase of each intervention and when (#19) |

## Writing values

Each stage sets its values whenever it updates. A value keeps its row until it is removed or its
section is cleared, so rows don't jump around while values change.

```cpp
#include "Debug/AdaptiveDebugSubsystem.h"

if (UAdaptiveDebugSubsystem* Debug = UAdaptiveDebugSubsystem::Get(this))
{
	Debug->SetBar(EAdaptiveDebugSection::Need, TEXT("Need.Combat"), CombatScore);    // [0, 1], clamped, drawn as a bar
	Debug->SetNumber(EAdaptiveDebugSection::Telemetry, TEXT("Player.Speed"), Speed);
	Debug->SetText(EAdaptiveDebugSection::Need, TEXT("Level"), TEXT("Elevated"));
}
```

The same functions are Blueprint-callable on *Get Game Instance Subsystem (Adaptive Debug
Subsystem)*. `UAdaptiveDebugSubsystem::IsOverlayVisible()` lets a stage skip work that only
feeds the overlay.

## Effect on gameplay

The overlay draws on the debug canvas (`UDebugDrawService`, the same layer as `showdebug`). It
spawns no actors, takes no input and changes no game state, so hiding it has no effect on
gameplay. Values are stored while it is hidden too (a small array write), so it shows the latest
state as soon as it is turned on. Its draw cost shows up as *Debug Overlay Draw* under
`stat AdaptiveCompanion`.

The overlay is not part of the session log; everything the evaluation needs is logged separately
([LOGGING.md](LOGGING.md)).

## Screenshots

`shot` captures the scene without the overlay, `shot showui` with it.

## Tests

`AdaptiveCompanion.Debug.Data` and `AdaptiveCompanion.Debug.ParseSection`:

```bash
UnrealEditor-Cmd AdaptiveCompanion.uproject -ExecCmds="Automation RunTests AdaptiveCompanion.Debug; Quit" -unattended -nullrhi
```

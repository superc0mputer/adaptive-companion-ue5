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
you. In `Lvl_Combat`, take three hits within 5 s and the companion runs to you with a bark: the
first, deliberately naive pass through the whole pipeline (damage → score → state → intervention
→ companion reaction), every step logged with a timestamp.

Each play session writes a log to `AdaptiveCompanion/Saved/AdaptiveLogs/<SessionId>.jsonl`, which
loads in Python with `pd.read_json(path, lines=True)`. Details: [docs/LOGGING.md](docs/LOGGING.md).

The game reports telemetry to the framework's pipeline, which runs its six stages at a fixed rate
(10 Hz) configured in `Content/Companion/DA_PipelineConfig` and hands the selected intervention
back to the companion. `ac.Pipeline.TestActor` spawns an actor that reports test values.
Details: [docs/PIPELINE.md](docs/PIPELINE.md#first-loop).

The game also tells the framework what the player is doing: `Context.Combat` while an enemy is
within 15 m (and for 3 s after), `Context.Navigation` otherwise. Context changes are logged and
shown in the overlay. Details: [docs/PIPELINE.md](docs/PIPELINE.md#gameplay-context).

In combat the game reports damage, health, attacks, hits on enemies, kills, deaths and how many
enemies are near and how close. Details: [docs/PIPELINE.md](docs/PIPELINE.md#combat-telemetry).

For navigation it reports the path distance to the current objective, whether the player heads
toward it, their speed, revisited areas and reached objectives, and tells the framework which
objective is current. Objectives are
`NavigationObjective` actors placed in a level. Details: [docs/PIPELINE.md](docs/PIPELINE.md#navigation-telemetry).

From the camera and input it reports how fast the player turns the camera, how far they look
away from the objective, how many nearby enemies are out of view, and how long they have given
no input. Details: [docs/PIPELINE.md](docs/PIPELINE.md#camera-and-input-telemetry).

The framework turns these raw signals into 14 features in [0, 1], where 1 always means "more of a
problem": e.g. recent damage, miss rate, enemies out of view, no progress toward the objective,
standing still, looking around. Each one aggregates a signal over a sliding window (count, sum,
ratio, latest, mean, slope, share below or above a threshold) and maps it from a fixed range to
[0, 1]. They are defined in `DA_PipelineConfig`, not in code, logged and shown in the overlay.
Details: [docs/PIPELINE.md](docs/PIPELINE.md#features).

From the features, the framework estimates four assistance needs in [0, 1]: combat (struggling in
a fight), threat (an enemy the player doesn't see), navigation (not getting closer to the
objective) and disorientation (not knowing where to go). Each need is a list of rules like
`Nav.StandingStill & Camera.LookingAround`; a rule fires with its weight times its weakest
feature, and the rules are combined by noisy-OR. A need only counts in its gameplay context
(combat needs in combat). The strongest rule is logged and shown in the overlay as the reason.
Rules, weights and contexts are in `DA_PipelineConfig`, and the estimator can be swapped (machine
learning in #43). Details: [docs/PIPELINE.md](docs/PIPELINE.md#assistance-need).

Each need is then smoothed over time (faster up than down) and mapped to a level, Normal, Elevated
or High, with separate thresholds for entering and leaving each level. A score that hovers around
a threshold or jumps back and forth no longer makes the level flicker, and a full-strength problem
still becomes Elevated within 0.5 s. Details: [docs/PIPELINE.md](docs/PIPELINE.md#need-levels).

The companion's actions are config assets that say which needs each one helps with, in which
contexts it fits, and how costly and intrusive it is: a navigation hint, a threat call-out and
drawing an enemy's attention. The companion registers them with the framework, which checks every
update whether each one is possible (right context, and the game says yes, e.g. an enemy is out
of view). Details: [docs/PIPELINE.md](docs/PIPELINE.md#companion-actions).

From the available actions the framework picks the best one with the utility from the exposé:
relevance to the elevated need times its score, plus suitability, minus a repetition penalty, the
current cost and intrusiveness (which counts less the more urgent the need). It does nothing when
no need is elevated, help is still running, the last help was under 5 s ago, nothing fits, or the
best action isn't good enough, and logs why. Details: [docs/PIPELINE.md](docs/PIPELINE.md#intervention-selection).

The selected action goes to the object that registered it, here the companion's AI controller,
which starts that action's own state in the companion's StateTree. The companion reports back
`started`, then `completed`, `failed` (with a reason) or `interrupted`, and returns to following
the player; every phase is logged with its time. A commandlet adds a StateTree state for every new
action. Details: [docs/PIPELINE.md](docs/PIPELINE.md#carrying-out-actions).

For repeatable tests there is a combat scenario: `Content/Scenarios/Lvl_CombatScenario`, an arena
where waves of 1, 2 and then 4 enemies attack, the last one from both sides. Its phases are
written to the session log. `Lvl_NavigationScenario` is a maze with four objectives in a row,
the third one without a beacon in a part full of dead ends and look-alike junctions, where players
can get lost. Details: [docs/SCENARIOS.md](docs/SCENARIOS.md).

`ac.Debug 1` in the console shows a debug overlay with the pipeline's live values;
`ac.Debug.Demo` fills it with test values. Details: [docs/DEBUG_OVERLAY.md](docs/DEBUG_OVERLAY.md).

## Repository layout

| Path | Contents |
|---|---|
| `AdaptiveCompanion/` | Unreal project (prototype game) |
| `AdaptiveCompanion/Plugins/AdaptiveCompanionFramework/` | Game-independent framework plugin |
| `AdaptiveCompanion/Source/AdaptiveCompanion/Companion/` | Companion character, AI controller and StateTree nodes |
| `AdaptiveCompanion/Source/AdaptiveCompanion/Context/` | Decides which gameplay context the game reports |
| `AdaptiveCompanion/Source/AdaptiveCompanion/Navigation/` | Navigation objectives and which one is current |
| `AdaptiveCompanion/Source/AdaptiveCompanion/Scenario/` | Test scenarios that run in a level and log their phases |
| `AdaptiveCompanion/Source/AdaptiveCompanion/Telemetry/` | Reports telemetry that doesn't belong to a single actor (enemies near the player, movement toward the objective, camera and input) |
| `AdaptiveCompanion/Content/Companion/` | Our own assets (`BP_Companion`, `ST_Companion`, `DA_PipelineConfig`), stored via Git LFS |
| `AdaptiveCompanion/Content/Scenarios/` | Our own test levels (`Lvl_CombatScenario`, `Lvl_NavigationScenario`), stored via Git LFS |
| `Scripts/` | Setup scripts, `rebuild_editor.sh` (quit the editor, build, reopen it) |
| `docs/` | Architecture, pipeline, scenarios, assets, logging and debug overlay documentation |

The framework lives in its own runtime plugin so it can be reused in other UE5 projects; the
game module depends on it, never the other way round. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

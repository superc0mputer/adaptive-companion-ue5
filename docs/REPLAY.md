# Record and replay

Since #24 every play session can be replayed through the pipeline without the game: the session
log records what the game gave the pipeline at every update, and a commandlet runs the stages on
that again. Replays are deterministic, and can be compared with labeled situations. They are the
basis for the classification test (H1, #25), for tuning (#16) and for training data (#43).

```
play session ──▶ Saved/AdaptiveLogs/<session>.jsonl        (input record per update, besides the usual records)
                       │
                       ▼
-run=AdaptiveReplay ──▶ stages 1–6 with the current DA_PipelineConfig, no game
                       │
                       ├─▶ <session>.replay.jsonl   needs, levels and selection per update
                       └─▶ <session>.replay.json    deterministic? same as live? agreement with <session>.labels.json
```

The code is in the framework plugin (`*/Replay/`): `FAdaptiveInputRecorder` and the input record
format (`AdaptiveInputRecording.h`), `UAdaptiveReplayRunner`, the labels and comparisons
(`AdaptiveReplayEvaluation.h`) and `UAdaptiveReplayCommandlet`.

## Recording

With *Record Inputs* on (Project Settings > Plugins > Adaptive Companion Pipeline, default on),
the pipeline writes a `recording_start` record (`format`, `update_rate`) at session start and one
`input` record after every update. Nothing else changes: it is the same session log, so a play
session still writes exactly one file.

An `input` record holds everything that comes from the game, and only what changed:

| Field | | When |
|---|---|---|
| `u`, `time` | The update's number and session time | always |
| `events` | `[name, value, time]` of every event since the previous update, with the time it was reported | when there were any |
| `values` | Values that changed or appeared | when any changed |
| `contexts` | All active contexts | first update, and when they changed |
| `objective` | `name` (empty: none), `x`, `y`, `z` in cm | first update, and when it changed |
| `actions` | `[name, asset path]` of every registered action | first update, and when the list changed |
| `answers` | Per action: `[possible, reason, cost]`, the game's answer to `CheckAction` (the cost the pipeline used) | when an answer changed; only actions the contexts allowed, since the game is only asked about those |
| `before` | `[intervention, phase, detail, time]` reported since the previous update | when there were any |
| `during` | The same, reported while the update ran, i.e. from inside `PerformAction` (this game's companion reports `started` a frame later, so it lands in `before`) | when there were any |
| `rejected` | The handler refused the intervention dispatched in this update | only then |

```json
{"u":157,"time":18.69992483407259,"values":{"Input.IdleTime":22,"Nav.ObjectiveDistance":9.9},"before":[["Action.NavigationHint","completed","player followed",18.6118084192276]]}
```

The time of every update is in the record, so a replay runs its updates at exactly the recorded
times, hitches included (in `maze-hint-followed` the second update came 0.75 s after the first, while the level loaded). The existing `signal` records aren't enough for that: values are logged
when they change but the update times aren't, and the game's answers about actions (e.g. the
current cost of drawing aggro) and the phases' update are missing. Recording costs about
**2.5–3 KB/s** on top of the rest of the log (10–15 KB/s), so about 2 MB for 10 minutes.

## Replaying

```bash
UnrealEditor-Cmd AdaptiveCompanion.uproject -run=AdaptiveReplay -Log=<session> [-Labels=<file>] [-Out=<folder>] -unattended -nullrhi
```

- `-Log`: a path, or a session id or file name in the log folder.
- `-Labels`: defaults to `<session>.labels.json` next to the log, if there is one.
- `-Out`: where the results go; defaults to the log's folder.

`UAdaptiveReplayRunner` creates fresh stages from the **current** config asset and feeds them the
recorded updates: events (with their recorded times) and values to Telemetry, contexts and the
objective to Context, the recorded actions (the current assets, loaded by path) through
`UAdaptiveReplayActionHandler`, which answers `CheckAction` and `PerformAction` as the game did, and
the recorded phases to Behavior, before or after the update as they happened. Updates run as fast
as possible: the commandlet replays a 33 s session twice and compares it in 0.25 s. There is no game instance, log or overlay, so
the result depends only on the recording and the config.

The commandlet:

1. **Replays twice and compares the two runs** value for value (`deterministic`). It returns 1 if
   they differ.
2. **Compares with the live session**: the need level changes (`state` records) and selections
   (`selected` phases) the live pipeline logged against the replay's, at most one update apart.
   With an unchanged config they are the same; after a change in rules or weights, the differences
   are listed.
3. **Compares with the labels**, if there are any (below).

It writes `<session>.replay.jsonl`, one line per update (`u`, `t`, per need `score`, `raw`, `level`,
`active`, `reason`, `contexts`, `decision`, `id`, `reason`, `running`), and `<session>.replay.json`,
the summary, and prints the summary.

## Labels

A labels file says what a recording should show:

```json
{"tolerance": 2.0, "labels": [
  {"from": 10.64, "to": 47.41, "needs": ["Disorientation"], "actions": ["Action.NavigationHint"], "note": "standing, looking around, ignoring the hint"}]}
```

- Within a label, its needs should be Elevated or High; every other need, and every need outside
  all labels, should be Normal.
- Updates within `tolerance` seconds after a label's start or end aren't counted, since the
  smoothed needs take a moment to follow. Their delay is measured separately.
- `actions` are optional: the actions that would fit.

The comparison gives, per need, the updates that agree and disagree (true and false positives and
negatives) with precision, recall and F1; and per label, the **detection delay** (seconds from its
start until each of its needs was Elevated, or never), the interventions selected in it, and
whether one of its actions was among them.

Labels can be written by hand. The example recordings got theirs from the scripts that played them:
the script logs `ac.Log.Marker label <needs> | <actions> | <note>` when a situation starts and
`ac.Log.Marker label end` when it ends, and the labels are read off the markers' times.

## Decisions

| Question | Decision | Why |
|---|---|---|
| What is recorded | The game's inputs per update, not the features | Features, needs and selection are what a replay computes. Recording them would make a replay with changed rules impossible |
| Where | In the session log, as `input` records | One file per session, as before: nothing to keep together with it, rename or match to a participant |
| Every update or only changes | A record every update, with only the changed inputs | The update times are needed for identical feature windows; the changes keep it small |
| Which config | The current one, not the one recorded | Trying changed rules and weights on the same recordings is what tuning (#16) and the estimators (#46, #47) need. `recorded_config` in the summary says which one the session used |
| The game in a replay | Its answers and phases replayed as recorded | Simulating the companion and the enemies would need the game. With the recorded config the replay selects what the live session did, so the recorded phases fit |
| Where labels are compared | In the commandlet | The loop from recording to agreement closes in one command; the wider analysis across sessions is #27 |

**Limits.** After changing the config, the replay can select interventions the live session
didn't, or at other times. The game's phases are then still the recorded ones, so whether an
intervention is "running" can differ from what the game would have done. Needs and levels up to the
first different selection are exact; after it, the needs still are (they don't depend on the
interventions), only the selection's "intervention still running" may differ.

## Example recordings

`Recordings/` holds three labeled recordings from scripted `-game` runs, as plain JSON Lines
(`.replay.*` results are ignored by git):

| Recording | What happens | Labels |
|---|---|---|
| `maze-hint-followed` | Maze: walking the path for 8 s, then standing and turning the camera until the hint, then walking the path | 10.8–15.5 s Disorientation |
| `maze-hint-ignored` | The same, but standing and turning the camera to the end, three hints in a row | 10.6–47.4 s Disorientation |
| `arena-pressure-wave` | Arena: the first two waves killed by the script, then the four-enemy wave on a player standing still | 17.5–55.4 s Threat |

Every one replays deterministically and reproduces the live session (all level changes and
selections). With the current config and a 2 s tolerance:

| Recording | Labeled need | Precision | Recall | F1 | Detection delay | Selected |
|---|---|---|---|---|---|---|
| `maze-hint-followed` | Disorientation | 0.01 | 0.04 | 0.02 | 4.7 s | hint (fits) |
| `maze-hint-ignored` | Disorientation | 1.00 | 0.92 | 0.96 | 4.6 s | 3 hints (fit) |
| `arena-pressure-wave` | Threat | 1.00 | 0.83 | 0.90 | 1.6 s | call-outs and drawing aggro (fit) |

The labels were set from the scripts before replaying, and the numbers show where the current
rules lag:

- **Disorientation takes 4.6–4.7 s** to become Elevated after the player stops and looks around,
  because `Nav.StandingStill` averages over 10 s.
- **It also stays Elevated for about 7 s** after the player walks on (fall half-life 3 s, exit
  threshold 0.35). In the short followed-hint label this outweighs the 1 update it was right, hence
  the low numbers there.
- **In the arena, Combat was Elevated for 10 s** (103 updates) although only Threat was labeled. A
  player who stands still among four enemies loses health, which the combat rules count. Whether
  that should count as a combat need is a labeling question for #25.

Tuning these is #16; the recordings make each change measurable.

## Tests

Automation tests `AdaptiveCompanion.Replay.Format` (only changes recorded, every field through
JSON and back, absent fields stay absent), `.Replay.Run` (a 20 s recording with the defaults:
selection, recorded end, two runs identical, rejected dispatch, impossible action, missing asset)
and `.Replay.Labels` (loading, tolerance, true and false positives and negatives, precision and
recall, detection delay, fitting action, a missed label).

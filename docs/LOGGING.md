# Session logging

Every pipeline stage writes to one central session log. It is used for tuning, for the technical
evaluation (H1, H2) and for the user study. The logger is part of the framework plugin
(`Plugins/AdaptiveCompanionFramework/Source/AdaptiveCompanionFramework/*/Logging/`).

## Where the files are

One play session writes exactly one file:

```
<Project>/Saved/AdaptiveLogs/<SessionId>.jsonl
```

- A session is one game instance: one PIE run in the editor, or one run of a game build from
  start to quit. Level changes (`open Lvl_Combat`) stay in the same file.
- `SessionId` is the UTC start time plus a random suffix, e.g. `20261020-131849-26d2e64a`, so
  files sort chronologically.
- The full path is printed at startup: `LogAdaptiveCompanionFramework: Display: Session log: ...`.
  In packaged builds `Saved` is the build's own Saved folder, so check that line.

## Record format

The file is [JSON Lines](https://jsonlines.org/): one JSON object per line, UTF-8.

```json
{"seq":2,"t":1.4955,"game_time":0.0005,"wall":"2026-10-20T13:18:51.033Z","session_id":"20261020-131849-26d2e64a","participant_id":"TEST01","type":"marker","payload":{"text":"hello world"}}
```

| Field | Meaning |
|---|---|
| `seq` | Record number within the session, starting at 0 and without gaps |
| `t` | Seconds since the session started (monotonic, doesn't pause or reset on level change). Use this for time series and latencies |
| `game_time` | World time of the current level in seconds (pauses, time dilation, restarts with every level). `null` if no level is loaded |
| `wall` | Wall clock, ISO 8601 UTC with milliseconds. Use this to join with questionnaire data |
| `session_id` | Same for every record of the file |
| `participant_id` | `-ParticipantId=P01` on the command line, otherwise *Default Participant Id* from the settings (`none`). Can be changed at runtime with `SetParticipantId` |
| `type` | Record type, see below |
| `payload` | Object with type-specific fields |

NaN and infinity in a payload are written as `null`, so every line stays valid JSON.

### Record types

| Type | Written by | Payload |
|---|---|---|
| `session_start` | Logger, first record | `project`, `project_version`, `plugin_version`, `engine_version`, `build_config`, `platform`, `editor`, `start_utc` |
| `level_start` | Logger, when a level begins play | `map`, `game_mode` |
| `pipeline_start` | Pipeline, at session start | `config`, `update_rate`, `pause_with_game`, `stages` ([PIPELINE.md](PIPELINE.md)) |
| `marker` | `ac.Log.Marker <text>` | `text` |
| `session_end` | Logger, last record | `duration` (s), `records` (including this one) |
| `signal` | Telemetry stage, for every reported event and every changed value | `name`, `kind` (`event` / `value`), `value` |
| `feature` | Feature stage, when a feature changed | `name`, `value` |
| `state` | Need stage, when a need's level changed | `need`, `score`, `level` (`Normal` / `Elevated`) |
| `intervention` | Selection and Behavior stages, and the game through `ReportInterventionPhase` | `name`, `phase` (`selected`, `dispatched`, `started`, `ended`), more fields per phase ([PIPELINE.md](PIPELINE.md)) |
| `context` | Context stage (#7) | Defined by the stage |

The type names are in `AdaptiveLogRecordTypes.h`. Games may log their own types.

If the process crashes, the file has no `session_end`, but every record up to the last flush
(at most *Flush Interval* old) is complete.

## Loading in Python

```python
import pandas as pd
df = pd.read_json("Saved/AdaptiveLogs/20261020-131849-26d2e64a.jsonl", lines=True)
data = df.join(pd.json_normalize(df.payload))   # payload fields as columns
```

## Writing records

From C++:

```cpp
#include "Logging/AdaptiveLogSubsystem.h"
#include "Logging/AdaptiveLogRecordTypes.h"

if (UAdaptiveLogSubsystem* Log = UAdaptiveLogSubsystem::Get(this))
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("name"), TEXT("Combat.DamageTaken"));
	Payload->SetNumberField(TEXT("value"), Damage);
	Log->LogRecord(AdaptiveLogRecordType::Signal, Payload);

	// or with a USTRUCT, whose properties become the payload fields
	Log->LogStruct(AdaptiveLogRecordType::State, MyStateStruct);
}
```

From Blueprint: *Get Game Instance Subsystem (Adaptive Log Subsystem)* → *Log Record* with a type
and maps of numbers and strings.

`Get` returns `nullptr` when logging is disabled, so always check it.

## Performance

Records are serialized to a string on the game thread and collected in memory. Every
*Flush Interval* seconds (or when the buffer exceeds *Max Pending KB*) the buffer is handed to a
background task that writes it; the game thread never waits for the disk. Writes run in order, so
lines never interleave.

Measured with `ac.Log.Benchmark` (Apple M4, Development build): **about 5 µs per record** for a
record with three payload fields; handing the buffer to the writer takes a few µs. Even 100 records
per frame cost about 0.5 ms. The cost shows up as *Log Record* under `stat AdaptiveCompanion` and
in Unreal Insights.

## Settings and commands

Project Settings > Plugins > Adaptive Companion Logging (`UAdaptiveLogSettings`, saved to
`DefaultGame.ini`):

| Setting | Default | |
|---|---|---|
| Enabled | true | Disables the logger entirely (no file is created) |
| Log Directory | `AdaptiveLogs` | Relative to the project's `Saved` folder, or an absolute path |
| Default Participant Id | `none` | Overridden by `-ParticipantId=` |
| Flush Interval | 1 s | How often pending records are written |
| Max Pending KB | 256 | Write earlier when this much is pending |

| Console command | |
|---|---|
| `ac.Log.Marker <text>` | Writes a `marker` record, e.g. to mark the start of a test section |
| `ac.Log.Benchmark [N]` | Writes N `signal` test records (default 10000) in one frame and prints the cost per record |

## Tests

`AdaptiveCompanion.Logging.RecordLine` and `AdaptiveCompanion.Logging.Writer` (Session Frontend >
Automation, or headless):

```bash
UnrealEditor-Cmd AdaptiveCompanion.uproject -ExecCmds="Automation RunTests AdaptiveCompanion.Logging; Quit" -unattended -nullrhi
```

# Technical measurements

How fast the companion reacts to a raised need, and what the framework costs per update (#25).
Both are measured in every session, written to the session log and exported as tables. They
answer H2 of the exposé: the selected help reaches the screen within a time suitable for real-time
play, at a cost the game doesn't notice.

![Overlay section 7: the hint's latency and the cost of the last 5 s, with the hint's trail on the floor](images/technical-measurements.png)

## Latency

Every intervention gets a `latency` record with the time of each step, from the need's raw score
reaching its threshold to the first frame on screen that shows the companion's reaction:

```
 telemetry          Need stage                 Selection   Behavior      game (StateTree)    render thread
 ─────────┬──────────────┬───────────────────────┬────────────┬──────────────┬──────────────────┬─────────
       threshold    level change              selected    dispatched      started           presented
          │  smoothing   │    selection wait     │  pipeline  │   handoff    │     render       │
          │              │                       ◄────────────┴── reaction ──┴──────────────────►
          │              ◄─────────────────────── response (H2) ─────────────────────────────────►
          ◄──────────────────────────────────────── total ──────────────────────────────────────►
```

| Step | When |
|---|---|
| `threshold` | The update in which the need's raw score reached the Elevated enter threshold, in the run of updates that led to the level change |
| `level_change` | The update in which the smoothed need rose from Normal: the state change |
| `selected` | The update that selected the intervention began |
| `dispatched` | The intervention was handed to the game's handler (`PerformAction`) |
| `started` | The game reported `started`: its behavior began and spawned its cues (trail, arrow, ...) |
| `presented` | The render thread finished the first frame drawn after `started` and was about to present it |

| Span | From → to | What it shows |
|---|---|---|
| `smoothing` | threshold → level change | The smoothing's delay, a design choice ([need levels](PIPELINE.md#need-levels)) |
| `selection_wait` | level change → selected | 0 when selected in the same update; longer while help is still running, the last one was less than 5 s ago, a cooldown runs or the game says the action isn't possible yet |
| `pipeline` | selected → dispatched | The update's own work up to the handler |
| `handoff` | dispatched → started | The companion's StateTree handles the intervention event on its next tick |
| `render` | started → presented | Game thread finishing the frame, render thread drawing it |
| `reaction` | selected → presented | Framework and game together, once the decision is made |
| `response` | level change → presented | **H2's response time**: state change to visible reaction |
| `total` | threshold → presented | Including the smoothing |

The record also has `frames` (`handoff`: game frames from selection to `started`; `render`: frames
from `started` to the presented frame, 0 = the very frame the cue was spawned in), `repeat` and,
if the intervention never reached the screen, `incomplete` (its final phase, e.g. `rejected`, or
`not presented`).

**Repeats.** While a need stays up, the same help can be selected again after it ended (the hint
the player ignored is shown again 15 s later). Threshold and level change then still belong to
the first selection of that episode, so such a record is marked `repeat: true`; its `reaction`
counts, its `selection_wait`, `response` and `total` don't describe its own reaction and the
summary leaves them out.

**Telemetry wait.** The pipeline runs at 10 Hz, so a reported event waits up to one interval (plus
the rest of the frame) for the next update. That wait comes before `threshold` and is reported
separately in the perf records (`input_wait_ms`, the longest wait of each update with events).

### How "on screen" is measured

`FAdaptivePresentProbe` listens to Slate's `OnBackBufferReadyToPresent`, which the render thread
calls for every window right before presenting it. On `started` the Behavior stage enqueues a
render command for the intervention; render commands run in order, and a frame's window is drawn
after everything the game enqueued during its tick, so the first present of the game's window after
that command is the frame that contains the cue. The time is taken on the render thread with the
same clock as the session log.

GPU execution and the display's scan-out happen after this point and are not included: on a
60 Hz display that adds roughly one to two frames. Where nothing is rendered (commandlets,
`-nullrhi`, the automation tests) there is no probe and the record ends at `started` with
`incomplete: not presented`. A frame that doesn't arrive within 1 s (window minimized) ends the same
way.

## Cost

With *Measure Performance* on (default) the pipeline times every update with `FPlatformTime::Cycles64`:
the whole update, each stage, and writing the input record ("Recording"). It also times every call
the game makes into its API between updates (`ReportEvent`, `ReportValue`, `SetContext`, ...), and
samples the engine's frame times (frame, game thread, render thread, GPU). Every *Performance Window*
(5 s) it logs a `perf` record, at the session's end a `perf_summary` over everything, and any single
update slower than *Spike Threshold* (5 ms) as a `perf_spike` with its stages' times.

| Field | |
|---|---|
| `update_us` | Per update: `count`, `mean`, `p50`, `p95`, `p99`, `max` (µs, nearest-rank percentiles) |
| `parts_us` | The same per stage (`Telemetry`, `Feature`, `Context`, `Need`, `Selection`, `Behavior`) and `Recording` |
| `api_us_per_frame`, `api_calls_per_frame` | The game's API calls between updates, per frame |
| `input_wait_ms` | How long events waited for their update (updates with events only) |
| `load_per_update` | `events`, `values`, `features`, `needs`, `actions`: the load the costs belong to |
| `frame_ms`, `game_thread_ms`, `render_thread_ms`, `gpu_ms` | The frames around it |
| `pipeline_us_per_frame` | Updates plus API time, per frame |
| `game_thread_share_pct` | The same as a share of the game thread's busy time |

The stage times include everything a stage does, logging included, and the Context stage includes
the game's answers to `CheckAction` (e.g. the hint's path query).

## Heavy load

Two ways, one with the game and one without:

- **More enemies.** `Scenario.ExtraEnemies <N>` spawns N extra enemies in the combat scenario on
  top of its waves (0 removes them). They attack like the wave's enemies and, like them, are
  removed when the player dies and spawn again when the wave restarts. This loads the game (AI,
  animation, rendering) and the game's telemetry (enemies near, out of view).
- **A bigger pipeline.** `-run=AdaptiveBenchmark` replays a recording with k copies of this game's
  features and need dimensions, each on its own copy of the telemetry (`Combat.DamageTaken#2`, ...),
  so k = 10 is ten times this game's load. It times every update's stages over several runs, without
  the game and without logging, so it shows what the computation itself costs and how it grows.

```bash
UnrealEditor-Cmd AdaptiveCompanion.uproject -run=AdaptiveBenchmark [-Log=Recordings/arena-pressure-wave.jsonl] [-Copies=1,2,5,10,20,50] [-Repeat=5] [-Out=<folder>]
```

## Tables

```bash
UnrealEditor-Cmd AdaptiveCompanion.uproject -run=AdaptiveMeasurements -Logs=<log, folder or session id>[,...] [-Out=<folder>]
```

reads the measurement records of one or more session logs and writes, by default to
`Saved/AdaptiveMeasurements/`:

| File | Rows |
|---|---|
| `latency.csv` | One per intervention: every step's time (s), every span (ms), frames, `repeat`, `incomplete` |
| `latency_summary.csv` | Per action and over all: how many, how many reached the screen and were repeats, median / p95 / max of the spans |
| `perf.csv` | One per perf window |
| `perf_summary.csv` | One per session |
| `measurements.md` | Both summaries as Markdown tables |

The benchmark writes `benchmark.csv` and `benchmark.md` to the same folder. Missing values are
empty cells. `-Logs` takes files, folders (every `.jsonl` in them except replay output) and session
ids, separated by commas.

## Settings

Project Settings > Plugins > Adaptive Companion Pipeline > Measurements (`UAdaptivePipelineSettings`):

| Setting | Default | |
|---|---|---|
| Measure Performance | true | Times updates, stages and API calls; logs `perf`, `perf_spike` and `perf_summary` |
| Performance Window | 5 s | Seconds per `perf` record |
| Spike Threshold | 5 ms | Updates slower than this are logged on their own |

Latency is always measured; it costs a few timestamps per intervention.

## Decisions

- **Every step, not one number.** The exposé measures "from the moment telemetry thresholds are
  reached to the execution of the companion's intervention". Smoothing, waiting for a free slot,
  the pipeline, the StateTree and rendering have very different causes and fixes, so each step is
  logged and the spans are derived from them. `response` (level change → screen) is H2's number:
  from the moment the framework decided the player needs help to the moment it is visible.
  `total` adds the smoothing, which is deliberate and tuned (#16), not a delay of the system.
- **"Visible" = the frame is ready to present**, measured on the render thread, rather than the
  `started` report. The cue is spawned at `started` but only seen when its frame is drawn, which in
  these runs was 50–160 ms later. GPU and display time can't be observed from the engine without
  vendor tools and are named as a limitation instead.
- **Measured in every session**, not in a separate benchmark mode, so the study sessions (#26)
  carry their own latencies and costs. The overhead is two cycle counter reads per stage and per
  API call.
- **Stage costs include logging.** The in-game numbers are what the framework costs as it runs in
  the study, log included. The benchmark shows the computation alone, so the two together say
  where the time goes.
- **Rejected:** measuring the latency from the event that pushed the need over the threshold.
  Features aggregate many signals over windows, so there is no single causing event; the input
  wait (event → update) is measured on its own instead, and how early a need is detected against
  labeled situations is the replay's detection delay ([REPLAY.md](REPLAY.md#labels), H1).

## Results

Apple M4, Development build, `-game` windowed at 1280 × 1150, scripted runs: the maze with the
player standing and looking around (navigation hints), the arena with the player standing still
(call-outs, draw aggro), and the same arena with 30 extra enemies.

**Latency** (15 interventions, all reached the screen; 5 repeats):

| Session | Frame | Interventions | pipeline | handoff | render | selection → screen | level change → screen (first per episode) |
|---|---|---|---|---|---|---|---|
| Maze | 28 ms | 3 hints | ≤ 0.5 ms | 1.1–2.6 ms, 1 frame | 51–63 ms | 53–66 ms | 466 ms |
| Arena | 40 ms | 3 call-outs, 1 draw aggro | ≤ 0.4 ms | 0.8–2.5 ms, 1 frame | 57–99 ms | 60–101 ms | 60 ms (call-out), 1.6 s (aggro) |
| Arena + 30 enemies | 67 ms | 5 call-outs, 3 draw aggro | ≤ 0.9 ms | 5.0–10.2 ms, 1 frame | 57–162 ms | 63–171 ms | 97–290 ms (call-outs), 5.1–8.0 s (aggro) |

- **Selection to screen: median 97 ms, max 171 ms.** Almost all of it is rendering: the cue is in
  the very frame it was spawned in (`frames.render` 0 everywhere), and that frame is presented
  about two frames later because the render thread works one frame behind the game thread. The
  framework's own share (pipeline) stays under 1 ms; the StateTree adds exactly one game frame.
- **Level change to screen**, H2's number: 60–466 ms for the first call-out or hint. The hint's
  466 ms includes 400 ms in which the game didn't offer it yet (its path check runs every 0.5 s);
  the later call-outs under load waited one or two updates (110–170 ms) until the rising need
  lifted their utility above the minimum of 0.5. Draw aggro waited 1.5–7.8 s
  because a call-out was running or had just ended (one help at a time, 5 s apart): a selection
  rule, not a delay.
- **Threshold to screen**, with the smoothing: 457–1273 ms for call-outs, 916 ms for the hint.
- **Telemetry wait**: events waited 81–83 ms on average for their update (max 101 ms, 177 ms
  under load); per event, damage waited 61–177 ms (mean 86 ms, 26 events). A uniform wait would
  average 50 ms, and the update runs after the world tick in the same frame, so short waits should
  occur too. Why they don't isn't clear from these runs; the input records hold each event's time
  and its update's, so the study sessions will show whether this holds.

**Cost per update** (in-game, logging included):

| Session | Updates | mean | p95 | p99 | max | per frame | share of game thread | frame | game thread | GPU |
|---|---|---|---|---|---|---|---|---|---|---|
| Maze | 450 | 185 µs | 290 µs | 398 µs | 1.7 ms | 51 µs | 0.99 % | 28.4 ms | 5.2 ms | 26.2 ms |
| Arena | 700 | 179 µs | 267 µs | 379 µs | 4.6 ms | 72 µs | 0.93 % | 39.7 ms | 7.7 ms | 37.3 ms |
| Arena + 30 enemies | 685 | 361 µs | 621 µs | 1.4 ms | 3.3 ms | 209 µs | 0.81 % | 66.5 ms | 26.0 ms | 49.7 ms |

- The pipeline takes about **1 % of the game thread** and well under 0.5 ms per update. With 30
  extra enemies the frame time more than doubles (GPU and AI), the pipeline's cost doubles too
  (more enemy telemetry, more logged values) but its share stays below 1 %.
- In the maze and the arena the maxima (1.7 and 4.6 ms) are single updates in the first 5 s
  after the level loaded. Under load, single updates of 1–3.3 ms recur every few windows (p99
  1.4 ms). None passed the 5 ms spike threshold.

**Benchmark** (`arena-pressure-wave.jsonl`, 519 updates, 10 runs per row after a warm-up, no logging):

| Copies | Features | Needs | mean | p95 | p99 | max | Feature stage | Need stage |
|---|---|---|---|---|---|---|---|---|
| 1 | 14 | 4 | 3.3 µs | 3.9 µs | 4.4 µs | 10.4 µs | 1.0 µs | 0.5 µs |
| 2 | 28 | 8 | 4.9 µs | 6.0 µs | 7.1 µs | 13.8 µs | 1.9 µs | 1.0 µs |
| 5 | 70 | 20 | 10.0 µs | 12.5 µs | 13.3 µs | 27.0 µs | 4.9 µs | 2.4 µs |
| 10 | 140 | 40 | 18.5 µs | 23.5 µs | 24.7 µs | 33.4 µs | 10.0 µs | 4.8 µs |
| 20 | 280 | 80 | 36.6 µs | 46.2 µs | 50.6 µs | 118.3 µs | 20.7 µs | 9.8 µs |
| 50 | 700 | 200 | 94.6 µs | 119.3 µs | 134.3 µs | 300.1 µs | 55.7 µs | 25.9 µs |

- The computation itself costs **3.3 µs per update** and grows linearly with the number of features
  and needs: 50 times this game's configuration still takes under 0.1 ms.
- So nearly all of the in-game cost (≈ 180 µs) is **logging** (building the JSON records of
  signals, features, needs, selections and the input recording) and, in the Context stage, the
  game's own answers to `CheckAction` (path queries, enemy scans), which the replay takes from the
  recording. The framework's computation is a few µs per update.
- Two more runs gave the same numbers within 0.2 µs; one earlier run was about a third faster
  throughout (2.3 µs at 1 copy, 64 µs at 50). macOS decides which of the M4's core types a process
  runs on, so single runs vary.

**Limitations.** One machine, scripted players, one display mode. GPU execution and display
scan-out after the present are not included. The frame rates in these runs (15–35 fps,
GPU-bound in a window at 1280 × 1150) are lower than in a packaged build, which makes `render`
longer than it would be for study participants; #26 measures the study build.

## Tests

`AdaptiveCompanion.Measurements.Latency` (steps and spans, ended before start, no probe, timeout,
flush), `.NeedTimes` (threshold and level-change times from the Need stage), `.Stage` (the Behavior
stage fills them at dispatch and marks repeats), `.Perf` (percentiles, windows, per frame and share),
`.Tables` (CSV and summary, repeats left out of the response), `.Copies` (the benchmark's copies
compute the same as the original and don't change the config).

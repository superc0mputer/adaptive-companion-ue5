# Log analysis

Python scripts that turn session logs into plots and tables (#27): one timeline per session with
the needs, their levels and the interventions, and tables across sessions for tuning (#16), the
replay's agreement with labels (#24) and the latency and cost measurements (#25). They read only
files that already exist: session logs, labels files, replay output. No Unreal needed.

![Timeline of arena-pressure-wave: contexts, the Threat label and the scenario's phases on top; each need's smoothed and raw score over its Elevated and High shading; the call-outs and drawing aggro at the bottom](images/log-analysis.png)

The code is in `analysis/`: `aclog.py` loads a session into pandas tables, `plots.py` draws them,
`analyze.py` is the command line.

## Running

```bash
pip install -r analysis/requirements.txt     # pandas, matplotlib, numpy
python analysis/analyze.py [<log, folder or session id> ...] [--out <folder>] [--features] [--no-plots]
```

- Logs are given like `-run=AdaptiveMeasurements -Logs=`: files, folders (every `.jsonl` in them except
  replay output) or session ids in `Saved/AdaptiveLogs`. Without arguments, all of `Saved/AdaptiveLogs`.
- `--out`: defaults to `AdaptiveCompanion/Saved/AdaptiveAnalysis/`.
- `--features`: under every need that was active, plot the features its rules use.
- Logs without pipeline updates are skipped. A session that appears twice (a recording copied out
  of `Saved` into `Recordings/`) is analysed once, in the copy with labels or replay output.
- Logs from every version since October load, including those without intervention ids (before
  #23), raw scores or thresholds.

```bash
python analysis/analyze.py Recordings --features      # the three labeled examples
python analysis/analyze.py 20261118-111029-33a1c8c5   # one session by id
```

## Per session: the timeline

`sessions/<session>.png`, one row per lane, all on the session time `t`:

| Lane | Shows |
|---|---|
| What the player is doing | The active gameplay contexts (`context` records); labeled situations, from `<log>.labels.json` or, without one, the log's `label` markers ([REPLAY.md](REPLAY.md#labels)); the scenario's phases as they start, and *player died* where a wave was lost |
| one per need | Smoothed score (thick), raw score (thin) and the level as shading: yellow Elevated, orange High. On the right, the thresholds: where a level is entered (dashed) and left again (dotted). A hatched bar on top where this need is labeled, i.e. should be raised. A need that was never active (wrong context for the whole session) is a single grey line saying so |
| features (`--features`) | The features that need's rules use |
| Companion interventions | One row per action. The bar runs from selection to the end of the help; the thin line after it is the window of up to 10 s in which its effect is measured, shorter when the next help or a context change came first. Above the bar: its id and how it ended (`player followed`, `threat in view`, ...). Below: the need it was selected for and that need's level at selection → after the window, in bold when it went back to Normal |
| Pipeline cost | Mean and p95 cost per update per 5 s `perf` window, if measured |

**Replay overlay.** If `<log>.replay.jsonl` lies next to the log and the replay differs from the
live session (another config, or other level changes or selections), each need also gets the
replay's score (dashed) and the replay's level as a thin strip along the bottom of the panel. When
it is identical, the plot just says so at the top right. After changing rules, weights or
thresholds, run `-run=AdaptiveReplay` and the analysis again: the live curves show what happened,
the dashed ones what the new config would have done. This is the loop for tuning (#16). The
thresholds drawn are the recorded session's.

## Across sessions: tables

| File | Rows |
|---|---|
| `sessions.csv` | One per session: maps, duration, updates, interventions and how many relieved their need, labels, replay |
| `needs.csv` | One per session and need: seconds active, in Normal, Elevated and High; `episodes` (rises from Normal). With labels: `labeled_s`, `raised_in_label_s` and `raised_outside_label_s` (Elevated or High inside and outside that need's labels) |
| `interventions.csv` | One per intervention: need, score, utility, the time of each phase, how it ended, duration, and from its outcome `relieved`, `cut_short`, the levels and the need's score at selection, end and after |
| `replay.csv` | One per replayed session and need, from `<log>.replay.json`: deterministic, same as live, same config, true / false positives and negatives, precision, recall, F1 (#24) |
| `replay_labels.csv` | One per label and need: detection delay, the interventions selected in it, whether one fit |
| `latency.csv` | One per `latency` record: every step's time (s), every span (ms), frames, `repeat`, `incomplete` (#25) |
| `latency_summary.csv` | Per action and over all: count, reached the screen, repeats, median / p95 / max of every span; repeats are left out of `selection_wait`, `response` and `total` |
| `perf.csv` | One per perf window: update cost (mean, p50, p95, p99, max), mean per stage, input wait, frame times, share of the game thread |
| `perf_summary.csv` | The same once per session, from `perf_summary` |
| `report.md` | The tables above as Markdown, interventions grouped by action, and the skipped logs |
| `latency.png`, `perf.png` | Selection → screen per intervention split into pipeline / handoff / render; cost per update and session split by stage |

The latency and cost tables are the same as those of `-run=AdaptiveMeasurements`
([MEASUREMENTS.md](MEASUREMENTS.md#tables)), computed from the same records; on the sessions in
MEASUREMENTS.md they give the same numbers (e.g. 185 / 290 / 398 µs mean / p95 / p99 in the maze).

## Using it in Python

```python
import sys; sys.path.insert(0, "analysis")
import aclog, plots

s = aclog.load("Recordings/maze-hint-followed.jsonl")
s.needs()                    # need records: t, need, score, raw, active, reason
s.level_intervals("Threat")  # start, end, level
s.interventions()            # one row per intervention, with its outcome
s.labels(), s.replay(), s.latency(), s.perf()
plots.session_timeline(s, features=True).savefig("maze.png")
```

## Decisions

| Question | Decision | Why |
|---|---|---|
| Language | Python with pandas and matplotlib, outside the Unreal project | The study analysis (#26, questionnaires, statistics) will be in Python too; the logs were designed to load with `pd.read_json(lines=True)` |
| Measurements | Computed from the log's `latency` and `perf` records, not read from the commandlet's CSVs | One command from logs to every table, without running Unreal first. The spans are already computed in the records, so only the summary statistics are repeated |
| Replay and labels | Read from `-run=AdaptiveReplay`'s output, not recomputed | The pipeline (features, rules, smoothing) only exists in C++. Reimplementing it would give two versions to keep in sync |
| Static images | PNG per session | They go into the thesis and the development log, and a 60 s session is readable at a glance. Interactive plots weren't needed for tuning |
| Output folder | `Saved/AdaptiveAnalysis/` | Next to `AdaptiveLogs` and `AdaptiveMeasurements`, ignored by git: study logs are personal data |

## Tests

```bash
python -m unittest analysis/test_analysis.py
```

Loads `arena-pressure-wave` (needs, interventions with outcome, levels covering the session,
labels), checks that labels read from markers match the labels file, and runs `analyze.py` end
to end on copies of the three recordings.

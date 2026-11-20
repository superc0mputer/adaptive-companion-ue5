#!/usr/bin/env python3
"""Plots and tables from Adaptive Companion session logs (#27).

    python analysis/analyze.py <log, folder or session id> [...] [--out DIR] [--features] [--no-plots]

For every session: a timeline plot (needs, levels, interventions, contexts, labels, cost). Across
all sessions: tables of the needs' levels, the interventions and their outcomes, the replay's
agreement with the labels (#24) and the latency and cost measurements (#25), plus report.md.
See docs/ANALYSIS.md.
"""

from __future__ import annotations

import argparse
import sys
import warnings
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aclog  # noqa: E402
from aclog import Session, overlap  # noqa: E402

DEFAULT_OUT = aclog.DEFAULT_LOG_DIR.parent / "AdaptiveAnalysis"
SPANS = ["smoothing", "selection_wait", "pipeline", "handoff", "render", "reaction", "response", "total"]
FIRST_ONLY = {"selection_wait", "response", "total"}  # a repeat's threshold belongs to the episode's first help


# ------------------------------------------------------------------ tables

def sessions_table(sessions: list[Session]) -> pd.DataFrame:
    rows = []
    for s in sessions:
        ivs = s.interventions()
        rows.append({"session": s.name, "session_id": s.session_id, "participant": s.participant_id,
                     "maps": " ".join(s.maps), "duration_s": round(s.end - s.start, 2),
                     "updates": len(s.of_type("input")) or None,
                     "interventions": len(ivs),
                     "relieved": int((ivs["relieved"] == True).sum()),  # noqa: E712
                     "labels": len(s.labels()), "replay": s.replay_path is not None})
    return pd.DataFrame(rows)


def _active_intervals(s: Session, need: str) -> pd.DataFrame:
    d = s.needs()
    d = d[d["need"] == need]
    changes = [(t, a) for t, a in zip(d["t"], d["active"])]
    iv = aclog._intervals(changes, s.end)
    return iv[iv["value"] == True]  # noqa: E712


def needs_table(sessions: list[Session]) -> pd.DataFrame:
    """Per session and need: time in each level, episodes, and with labels, time raised inside and outside them."""
    rows = []
    for s in sessions:
        labels = s.labels()
        for need in s.need_names:
            levels = s.level_intervals(need)
            raised = levels[levels["value"].isin(["Elevated", "High"])]
            row = {"session": s.name, "need": need,
                   "active_s": round(overlap(_active_intervals(s, need), s.start, s.end), 2)}
            for level in aclog.LEVELS:
                row[f"{level.lower()}_s"] = round(overlap(levels[levels["value"] == level], s.start, s.end), 2)
            values = levels["value"].to_list()
            row["episodes"] = sum(1 for a, b in zip(values, values[1:]) if a == "Normal" and b != "Normal")
            mine = labels[labels["needs"].apply(lambda n: need in n)] if not labels.empty else labels
            if not labels.empty:
                in_label = sum(overlap(raised, a, b) for a, b in zip(mine["from"], mine["to"]))
                row["labeled_s"] = round(float((mine["to"] - mine["from"]).sum()), 2)
                row["raised_in_label_s"] = round(in_label, 2)
                row["raised_outside_label_s"] = round(row["elevated_s"] + row["high_s"] - in_label, 2)
            rows.append(row)
    return pd.DataFrame(rows)


def interventions_table(sessions: list[Session]) -> pd.DataFrame:
    frames = [s.interventions() for s in sessions]
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def replay_tables(sessions: list[Session]) -> tuple[pd.DataFrame, pd.DataFrame]:
    """The replay's checks and its agreement with the labels, from `<log>.replay.json` (#24)."""
    agreement, labels = [], []
    for s in sessions:
        summary = s.replay_summary()
        if not summary:
            continue
        base = {"session": s.name, "deterministic": summary.get("deterministic"),
                "matches_live": summary.get("live", {}).get("matches"),
                "same_config": summary.get("recorded_config") == summary.get("replay_config")}
        lab = summary.get("labels") or {}
        for need, m in (lab.get("needs") or {"": {}}).items():
            agreement.append({**base, "need": need, **{k: m.get(k) for k in
                              ["tp", "fp", "fn", "tn", "precision", "recall", "f1"]}})
        for item in lab.get("labels", []):
            for need, delay in (item.get("delays") or {}).items():
                labels.append({"session": s.name, "from": item["from"], "to": item["to"], "note": item.get("note", ""),
                               "need": need, "detection_delay_s": delay,
                               "selected": ", ".join(item.get("selected", [])),
                               "fitting_action": item.get("fitting_action")})
    return pd.DataFrame(agreement), pd.DataFrame(labels)


def latency_tables(sessions: list[Session]) -> tuple[pd.DataFrame, pd.DataFrame]:
    """`latency` records as rows, and per action and over all: count, reached the screen, repeats and
    median / p95 / max of each span. Repeats are left out of the spans that start at the threshold or
    level change, as in -run=AdaptiveMeasurements (docs/MEASUREMENTS.md#latency)."""
    frames = [s.latency() for s in sessions]
    frames = [f for f in frames if not f.empty]
    if not frames:
        return pd.DataFrame(), pd.DataFrame()
    lat = pd.concat(frames, ignore_index=True)

    def summarize(group: pd.DataFrame, name: str) -> dict:
        row = {"action": name, "count": len(group),
               "presented": int((group["incomplete"].fillna("") == "").sum()),
               "repeats": int(group["repeat"].sum())}
        for span in SPANS:
            col = f"{span}_ms"
            if col not in group:
                continue
            values = group.loc[~group["repeat"], col] if span in FIRST_ONLY else group[col]
            values = values.dropna()
            row[f"{span}_median"] = round(float(values.median()), 2) if len(values) else None
            row[f"{span}_p95"] = round(float(np.percentile(values, 95, method="inverted_cdf")), 2) if len(values) else None
            row[f"{span}_max"] = round(float(values.max()), 2) if len(values) else None
        return row

    summary = [summarize(g, name) for name, g in lat.groupby("name")] + [summarize(lat, "all")]
    return lat, pd.DataFrame(summary)


def perf_tables(sessions: list[Session]) -> tuple[pd.DataFrame, pd.DataFrame]:
    frames = [s.perf() for s in sessions]
    frames = [f for f in frames if not f.empty]
    perf = pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()
    summary = pd.DataFrame([row for s in sessions if (row := s.perf_summary())])
    return perf, summary


# ------------------------------------------------------------------ report

def _md(df: pd.DataFrame, columns: list[str] | None = None) -> str:
    if df.empty:
        return "_none_\n"
    d = df[[c for c in (columns or list(df.columns)) if c in df.columns]].copy()
    for c in d.columns:
        if d[c].dtype.kind == "f":
            d[c] = d[c].map(lambda v: "" if pd.isna(v) else f"{v:.3g}" if abs(v) < 1000 else f"{v:.0f}")
    d = d.fillna("").astype(str)
    lines = ["| " + " | ".join(d.columns) + " |", "|" + "---|" * len(d.columns)]
    lines += ["| " + " | ".join(r) + " |" for r in d.itertuples(index=False)]
    return "\n".join(lines) + "\n"


def write_report(out: Path, tables: dict[str, pd.DataFrame], skipped: list[str]) -> None:
    ivs = tables["interventions"]
    by_action = pd.DataFrame()
    if not ivs.empty:
        by_action = ivs.groupby("name").agg(
            count=("id", "size"),
            ended=("end_phase", lambda x: ", ".join(f"{k} {v}" for k, v in x.value_counts().items())),
            relieved=("relieved", lambda x: int((x == True).sum())),  # noqa: E712
            median_duration_s=("duration", "median")).reset_index()
    text = ["# Session analysis\n",
            f"{len(tables['sessions'])} sessions. Tables as CSV next to this file; one timeline per session in `sessions/`.\n",
            "## Sessions\n", _md(tables["sessions"]),
            "## Needs\n", "Seconds in each level; `episodes` counts rises from Normal. With labels, "
            "`raised_*` is Elevated or High inside and outside the labels for that need.\n",
            _md(tables["needs"]),
            "## Interventions\n", _md(by_action),
            "## Replay agreement (#24)\n", _md(tables["replay"]),
            "### Detection delay per label\n", _md(tables["replay_labels"]),
            "## Latency (#25)\n", "Spans in ms. `selection_wait`, `response` and `total` leave out repeats.\n",
            _md(tables["latency_summary"], ["action", "count", "presented", "repeats",
                                            "reaction_median", "reaction_p95", "reaction_max",
                                            "response_median", "response_p95", "response_max",
                                            "render_median", "pipeline_max"]),
            "## Cost per update (#25)\n",
            _md(tables["perf_summary"], ["session", "updates", "update_us_mean", "update_us_p95", "update_us_p99",
                                         "update_us_max", "pipeline_us_per_frame", "game_thread_share_pct",
                                         "frame_ms_mean", "input_wait_ms_mean"])]
    if skipped:
        text += ["## Skipped\n", "\n".join(f"- {s}" for s in skipped) + "\n"]
    (out / "report.md").write_text("\n".join(text), encoding="utf-8")


# -------------------------------------------------------------------- main

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", default=[str(aclog.DEFAULT_LOG_DIR)],
                    help="session logs, folders or session ids (default: Saved/AdaptiveLogs)")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"output folder (default: {DEFAULT_OUT})")
    ap.add_argument("--features", action="store_true", help="plot each need's rule features under it")
    ap.add_argument("--no-plots", action="store_true", help="tables only")
    args = ap.parse_args(argv)
    warnings.simplefilter("ignore", FutureWarning)  # pandas: concat of tables with all-empty columns

    sessions, skipped = aclog.load_all(args.logs)
    if not sessions:
        print("No sessions with pipeline updates found.", file=sys.stderr)
        return 1
    args.out.mkdir(parents=True, exist_ok=True)

    lat, lat_summary = latency_tables(sessions)
    perf, perf_summary = perf_tables(sessions)
    replay, replay_labels = replay_tables(sessions)
    tables = {"sessions": sessions_table(sessions), "needs": needs_table(sessions),
              "interventions": interventions_table(sessions), "replay": replay, "replay_labels": replay_labels,
              "latency": lat, "latency_summary": lat_summary, "perf": perf, "perf_summary": perf_summary}
    for name, df in tables.items():
        if not df.empty:
            df.to_csv(args.out / f"{name}.csv", index=False)
    write_report(args.out, tables, skipped)

    if not args.no_plots:
        import matplotlib.pyplot as plt
        import plots
        (args.out / "sessions").mkdir(exist_ok=True)
        for s in sessions:
            fig = plots.session_timeline(s, features=args.features)
            fig.savefig(args.out / "sessions" / f"{s.name}.png", dpi=130)
            plt.close(fig)
        if not lat.empty:
            plots.latency_figure(lat).savefig(args.out / "latency.png", dpi=130)
        if not perf_summary.empty:
            plots.perf_figure(perf_summary).savefig(args.out / "perf.png", dpi=130)

    print(f"{len(sessions)} sessions -> {args.out}")
    empty = [line for line in skipped if line.endswith("no pipeline updates")]
    if empty:
        print(f"  skipped {len(empty)} logs without pipeline updates (listed in report.md)")
    for line in skipped:
        if line not in empty:
            print(f"  skipped {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

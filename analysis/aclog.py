"""Load Adaptive Companion session logs (docs/LOGGING.md) into pandas tables.

A `Session` holds one session log, plus its labels (`<log>.labels.json`) and replay output
(`<log>.replay.jsonl` / `.replay.json`, written by `-run=AdaptiveReplay`) if they lie next to it.
Everything is keyed by the session time `t` (seconds since the session started).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import pandas as pd

LEVELS = ["Normal", "Elevated", "High"]
FINAL_PHASES = {"completed", "failed", "interrupted", "timed_out", "rejected"}
LABEL_MARKER = "label "
DEFAULT_LOG_DIR = Path(__file__).resolve().parent.parent / "AdaptiveCompanion" / "Saved" / "AdaptiveLogs"


def _read_jsonl(path: Path) -> list[dict]:
    records = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    records.append(json.loads(line))
                except json.JSONDecodeError:
                    break  # a crashed session can end in a half-written line
    return records


def _intervals(changes: list[tuple[float, object]], end: float) -> pd.DataFrame:
    """Turn (time, value) changes into [start, end) intervals of constant value."""
    rows = []
    for i, (t, value) in enumerate(changes):
        stop = changes[i + 1][0] if i + 1 < len(changes) else end
        if stop > t:
            rows.append({"start": t, "end": stop, "value": value})
    return pd.DataFrame(rows, columns=["start", "end", "value"])


@dataclass
class Session:
    path: Path
    records: list[dict]
    labels_path: Path | None = None
    replay_path: Path | None = None
    replay_summary_path: Path | None = None
    _cache: dict = field(default_factory=dict, repr=False)

    # ---------------------------------------------------------------- basics

    @property
    def session_id(self) -> str:
        return self.records[0]["session_id"] if self.records else self.path.stem

    @property
    def name(self) -> str:
        """The file name without extension: the recording's name, or the session id."""
        return self.path.stem

    @property
    def participant_id(self) -> str:
        return self.records[0].get("participant_id", "none") if self.records else "none"

    def of_type(self, kind: str) -> list[dict]:
        return [r for r in self.records if r["type"] == kind]

    def first(self, kind: str) -> dict | None:
        return next((r for r in self.records if r["type"] == kind), None)

    @property
    def maps(self) -> list[str]:
        return [r["payload"]["map"] for r in self.of_type("level_start")]

    @property
    def start(self) -> float:
        """Time of the pipeline's first update (the first need record)."""
        need = self.first("need")
        return need["t"] if need else 0.0

    @property
    def end(self) -> float:
        return self.records[-1]["t"] if self.records else 0.0

    @property
    def has_pipeline(self) -> bool:
        return self.first("need") is not None

    # ----------------------------------------------------------- definitions

    @property
    def need_names(self) -> list[str]:
        defs = self.first("need_definitions")
        if defs and defs["payload"].get("dimensions"):
            return [d["name"] for d in defs["payload"]["dimensions"]]
        return sorted({r["payload"]["need"] for r in self.of_type("need")})

    def level_settings(self, need: str) -> dict:
        """Smoothing and thresholds the live session used for this need."""
        defs = self.first("need_definitions")
        if not defs:
            return {}
        payload = defs["payload"]
        return payload.get("dimension_levels", {}).get(need) or payload.get("levels", {})

    def required_contexts(self, need: str) -> list[str]:
        defs = self.first("need_definitions")
        for dim in (defs or {}).get("payload", {}).get("dimensions", []):
            if dim["name"] == need:
                return list(dim.get("required_contexts", []))
        return []

    def rule_features(self, need: str) -> list[str]:
        """The features the need's rules use, in the order they first appear."""
        defs = self.first("need_definitions")
        out: list[str] = []
        for dim in (defs or {}).get("payload", {}).get("dimensions", []):
            if dim["name"] == need:
                for rule in dim.get("rules", []):
                    for feature in rule.get("features", []):
                        if feature not in out:
                            out.append(feature)
        return out

    # ------------------------------------------------------------ time series

    def needs(self) -> pd.DataFrame:
        """Live need records: t, need, score (smoothed), raw, active, reason. Logged on change."""
        if "needs" not in self._cache:
            rows = [{"t": r["t"], **r["payload"]} for r in self.of_type("need")]
            self._cache["needs"] = pd.DataFrame(rows, columns=["t", "need", "score", "raw", "active", "reason"])
        return self._cache["needs"]

    def features(self) -> pd.DataFrame:
        rows = [{"t": r["t"], **r["payload"]} for r in self.of_type("feature")]
        return pd.DataFrame(rows, columns=["t", "name", "value", "raw"])

    def level_intervals(self, need: str) -> pd.DataFrame:
        """Live level of a need as intervals (start, end, value), from the first update to the end."""
        changes: list[tuple[float, object]] = [(self.start, "Normal")]
        for r in self.of_type("state"):
            if r["payload"]["need"] == need:
                changes.append((r["t"], r["payload"]["level"]))
        return _intervals(changes, self.end)

    def contexts(self) -> pd.DataFrame:
        """Intervals of each gameplay context: start, end, value (context name)."""
        rows = []
        open_since: dict[str, float] = {}
        for r in self.of_type("context"):
            now = set(r["payload"].get("contexts", []))
            for name in list(open_since):
                if name not in now:
                    rows.append({"start": open_since.pop(name), "end": r["t"], "value": name})
            for name in now:
                open_since.setdefault(name, r["t"])
        for name, start in open_since.items():
            rows.append({"start": start, "end": self.end, "value": name})
        return pd.DataFrame(rows, columns=["start", "end", "value"]).sort_values("start", ignore_index=True)

    def scenario_phases(self) -> pd.DataFrame:
        """Scenario records (this game's `scenario` type): t, scenario, phase, event."""
        rows = [{"t": r["t"], **r["payload"]} for r in self.of_type("scenario")]
        return pd.DataFrame(rows).reindex(columns=["t", "scenario", "phase", "event"] + sorted(
            {k for row in rows for k in row} - {"t", "scenario", "phase", "event"}))

    def markers(self) -> pd.DataFrame:
        """Markers that aren't label markers: t, text."""
        rows = [{"t": r["t"], "text": r["payload"]["text"]} for r in self.of_type("marker")
                if not r["payload"]["text"].startswith(LABEL_MARKER) and r["payload"]["text"] != "label end"]
        return pd.DataFrame(rows, columns=["t", "text"])

    # ---------------------------------------------------------- interventions

    def interventions(self) -> pd.DataFrame:
        """One row per intervention id: when each phase happened, how it ended, and its outcome."""
        by_id: dict[int, dict] = {}
        open_by_name: dict[str, int] = {}  # logs from before #23 have no ids: a phase belongs to its action's last selection
        for r in self.of_type("intervention"):
            p = r["payload"]
            if "id" not in p:
                if p["phase"] == "selected" or p["name"] not in open_by_name:
                    open_by_name[p["name"]] = len(by_id) + 1
                p = {**p, "id": open_by_name[p["name"]]}
            row = by_id.setdefault(p["id"], {"session": self.name, "id": p["id"], "name": p["name"]})
            row[p["phase"]] = r["t"]
            if p["phase"] == "selected":
                row.update(need=p.get("need"), score=p.get("score"), level=p.get("level"), utility=p.get("utility"))
            if p["phase"] in FINAL_PHASES:
                row.update(end_phase=p["phase"], end=r["t"], detail=p.get("detail", ""), duration=p.get("duration"))
        for r in self.of_type("outcome"):
            p = r["payload"]
            row = by_id.get(p["id"])
            if row is None:
                continue
            row.update(relieved=p.get("relieved"), cut_short=p.get("cut_short", ""),
                       after_duration=p.get("after_duration"),
                       level_baseline=p.get("level", {}).get("baseline"),
                       level_end=p.get("level", {}).get("end"),
                       level_after=p.get("level", {}).get("after"))
            need = p.get("measures", {}).get(p.get("need"), {})
            row.update(need_baseline=need.get("baseline"), need_end=need.get("end"),
                       need_after=need.get("after"), need_change=need.get("change"))
        cols = ["session", "id", "name", "need", "score", "level", "utility", "selected", "dispatched", "started",
                "end", "end_phase", "detail", "duration", "relieved", "cut_short", "after_duration",
                "level_baseline", "level_end", "level_after", "need_baseline", "need_end", "need_after", "need_change"]
        return pd.DataFrame(list(by_id.values())).reindex(columns=cols)

    # ------------------------------------------------------------- measurements

    def latency(self) -> pd.DataFrame:
        """One row per `latency` record (#25): times (s), spans (ms), frames."""
        rows = []
        for r in self.of_type("latency"):
            p = r["payload"]
            row = {"session": self.name, "id": p["id"], "name": p["name"], "need": p.get("need"),
                   "repeat": bool(p.get("repeat")), "incomplete": p.get("incomplete", "")}
            row.update({f"t_{k}": v for k, v in p.get("times", {}).items()})
            row.update({f"{k}_ms": v for k, v in p.get("ms", {}).items()})
            row.update({f"frames_{k}": v for k, v in p.get("frames", {}).items()})
            rows.append(row)
        return pd.DataFrame(rows)

    def perf(self) -> pd.DataFrame:
        """One row per `perf` window (#25): update cost, per stage means, frame times."""
        return pd.DataFrame([_perf_row(self.name, r["t"], r["payload"]) for r in self.of_type("perf")])

    def perf_summary(self) -> dict | None:
        r = self.first("perf_summary")
        return _perf_row(self.name, r["t"], r["payload"]) if r else None

    # ------------------------------------------------------- labels and replay

    def labels(self) -> pd.DataFrame:
        """Labeled situations: from, to, needs, actions, note. From the labels file, else the markers."""
        if self.labels_path:
            data = json.loads(self.labels_path.read_text(encoding="utf-8"))
            rows = data.get("labels", [])
        else:
            rows, current = [], None
            for r in self.of_type("marker"):
                text = r["payload"]["text"]
                if text == "label end" and current:
                    current["to"] = r["t"]
                    rows.append(current)
                    current = None
                elif text.startswith(LABEL_MARKER):
                    if current:
                        current["to"] = r["t"]
                        rows.append(current)
                    parts = [s.strip() for s in text[len(LABEL_MARKER):].split("|")]
                    current = {"from": r["t"], "needs": [n.strip() for n in parts[0].split(",") if n.strip()],
                               "actions": [a.strip() for a in parts[1].split(",")] if len(parts) > 1 else [],
                               "note": parts[2] if len(parts) > 2 else ""}
            if current:
                current["to"] = self.end
                rows.append(current)
        return pd.DataFrame(rows).reindex(columns=["from", "to", "needs", "actions", "note"])

    @property
    def label_tolerance(self) -> float:
        if self.labels_path:
            return float(json.loads(self.labels_path.read_text(encoding="utf-8")).get("tolerance", 2.0))
        return 2.0

    def replay(self) -> pd.DataFrame | None:
        """The replay's needs per update (long form): u, t, need, score, raw, level, active, decision."""
        if not self.replay_path:
            return None
        rows = []
        for r in _read_jsonl(self.replay_path):
            for need, v in r.get("needs", {}).items():
                rows.append({"u": r["u"], "t": r["t"], "need": need, **v, "decision": r.get("decision", "none")})
        return pd.DataFrame(rows)

    def replay_summary(self) -> dict | None:
        if not self.replay_summary_path:
            return None
        return json.loads(self.replay_summary_path.read_text(encoding="utf-8"))


def _perf_row(session: str, t: float, p: dict) -> dict:
    row = {"session": session, "t": t, "duration": p.get("duration"), "updates": p.get("updates"),
           "frames": p.get("frames")}
    for k in ["mean", "p50", "p95", "p99", "max"]:
        row[f"update_us_{k}"] = p.get("update_us", {}).get(k)
    for stage, s in p.get("parts_us", {}).items():
        row[f"{stage}_us_mean"] = s.get("mean")
    row["api_us_per_frame"] = p.get("api_us_per_frame", {}).get("mean")
    row["input_wait_ms_mean"] = p.get("input_wait_ms", {}).get("mean") if p.get("input_wait_ms", {}).get("count") else None
    row["input_wait_ms_max"] = p.get("input_wait_ms", {}).get("max") if p.get("input_wait_ms", {}).get("count") else None
    row["frame_ms_mean"] = p.get("frame_ms", {}).get("mean")
    for k in ["game_thread_ms", "render_thread_ms", "gpu_ms", "pipeline_us_per_frame", "game_thread_share_pct"]:
        row[k] = p.get(k)
    return row


# ------------------------------------------------------------------- loading

def _sibling(log: Path, suffix: str) -> Path | None:
    candidate = log.with_name(log.stem + suffix)
    return candidate if candidate.exists() else None


def load(path: str | Path) -> Session:
    path = Path(path)
    return Session(path=path, records=_read_jsonl(path),
                   labels_path=_sibling(path, ".labels.json"),
                   replay_path=_sibling(path, ".replay.jsonl"),
                   replay_summary_path=_sibling(path, ".replay.json"))


def find_logs(specs: list[str], log_dir: Path = DEFAULT_LOG_DIR) -> list[Path]:
    """Files, folders (every .jsonl in them except replay output) and session ids, like -run=AdaptiveMeasurements."""
    out: list[Path] = []
    for spec in specs:
        p = Path(spec).expanduser()
        if p.is_dir():
            out += sorted(f for f in p.glob("*.jsonl") if not f.name.endswith(".replay.jsonl"))
        elif p.is_file():
            out.append(p)
        elif (log_dir / spec).is_file():
            out.append(log_dir / spec)
        elif (log_dir / f"{spec}.jsonl").is_file():
            out.append(log_dir / f"{spec}.jsonl")
        else:
            raise FileNotFoundError(f"No log, folder or session id '{spec}' (looked in {log_dir})")
    return out


def load_all(specs: list[str], log_dir: Path = DEFAULT_LOG_DIR) -> tuple[list[Session], list[str]]:
    """Load every log once. A session that appears twice (e.g. a recording copied out of Saved) is kept
    in the copy with labels or replay output. Logs without pipeline updates are skipped with a note."""
    sessions: dict[str, Session] = {}
    skipped: list[str] = []
    for path in find_logs(specs, log_dir):
        s = load(path)
        if not s.has_pipeline:
            skipped.append(f"{path.name}: no pipeline updates")
            continue
        known = sessions.get(s.session_id)
        score = lambda x: (x.labels_path is not None) + (x.replay_path is not None)
        if known is None or score(s) > score(known):
            if known is not None:
                skipped.append(f"{known.path.name}: same session as {path.name}")
            sessions[s.session_id] = s
        else:
            skipped.append(f"{path.name}: same session as {known.path.name}")
    return sorted(sessions.values(), key=lambda s: s.session_id), skipped


# ------------------------------------------------------------------ helpers

def overlap(intervals: pd.DataFrame, start: float, end: float) -> float:
    """Seconds of [start, end) covered by the intervals."""
    if intervals.empty:
        return 0.0
    lo = np.maximum(intervals["start"].to_numpy(), start)
    hi = np.minimum(intervals["end"].to_numpy(), end)
    return float(np.clip(hi - lo, 0, None).sum())

"""Plots of one session over time, and of the measurements across sessions."""

from __future__ import annotations

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
from matplotlib.lines import Line2D
import numpy as np
import pandas as pd

from aclog import Session

# Reference palette (dataviz skill, light mode). Actions take categorical slots that stay clear of the
# level colours; levels use the status steps warning (Elevated) and serious (High), always with a label.
SERIES = ["#2a78d6", "#1baf7a", "#6250d6", "#e87ba4", "#008300", "#eda100"]
ACTION_SLOTS = ["#2a78d6", "#1baf7a", "#6250d6"]
CONTEXT_COLORS = {"Context.Combat": ("#e87ba4", 0.32), "Context.Navigation": ("#008300", 0.16)}
LEVEL_COLOR = {"Elevated": "#fab219", "High": "#ec835a"}
LEVEL_ALPHA = {"Elevated": 0.20, "High": 0.32}
SCORE = "#2a78d6"
REPLAY = "#0b0b0b"
INK = "#0b0b0b"
INK_2 = "#52514e"
MUTED = "#8a8984"
GRID = "#e6e5e1"
SURFACE = "#fcfcfb"

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": INK_2, "xtick.color": INK_2, "ytick.color": INK_2,
    "text.color": INK, "font.size": 9, "axes.titlesize": 10, "axes.titleweight": "bold",
    "axes.spines.top": False, "axes.spines.right": False, "legend.frameon": False,
})


def _action_colors(session: Session) -> dict[str, str]:
    names: list[str] = []
    reg = session.first("action_registered")
    if reg:
        names = [a["name"] for a in reg["payload"].get("actions", [])]
    for n in session.interventions()["name"].dropna():
        if n not in names:
            names.append(n)
    slots = ACTION_SLOTS + [c for c in SERIES if c not in ACTION_SLOTS]
    return {n: slots[i % len(slots)] for i, n in enumerate(names)}


def _short(name: str) -> str:
    return name.split(".", 1)[1] if "." in name else name


def _step(df: pd.DataFrame, end: float, column: str) -> tuple[np.ndarray, np.ndarray]:
    """Change-logged values as a step line that holds the last value to the end."""
    t = np.append(df["t"].to_numpy(), end)
    v = np.append(df[column].to_numpy(dtype=float), df[column].iloc[-1] if len(df) else np.nan)
    return t, v


def _ever_active(session: Session, need: str) -> bool:
    d = session.needs()
    return bool(d.loc[d["need"] == need, "active"].any())


def _panel_label(ax, text: str, note: str = ""):
    ax.text(0.004, 0.96, text, transform=ax.transAxes, ha="left", va="top", fontsize=9.5, fontweight="bold",
            color=INK, bbox=dict(facecolor=SURFACE, edgecolor="none", alpha=0.85, pad=1.5), zorder=5)


def _lane_title(ax, title: str, explanation: str = "", below: bool = False):
    """Lane title on the left; the explanation on the right, or on its own line under the title."""
    if explanation and below:
        ax.set_title(title, loc="left", fontsize=9.5, pad=15)
        ax.annotate(explanation, xy=(0, 1), xycoords="axes fraction", xytext=(0, 3), textcoords="offset points",
                    va="bottom", fontsize=8, color=INK_2)
        return
    ax.set_title(title, loc="left", fontsize=9.5, pad=4)
    if explanation:
        ax.set_title(explanation, loc="right", fontsize=8, fontweight="normal", color=INK_2, pad=4)


def session_timeline(session: Session, features: bool = False, show_perf: bool = True):
    """One figure: situation (contexts, labels, scenario), each need with its level, interventions,
    optionally the rule features under each need, and the pipeline's cost per perf window."""
    needs = session.need_names
    replay = session.replay()
    replay_note = ""
    summary = session.replay_summary()
    if replay is not None and summary and summary.get("live", {}).get("matches") \
            and summary.get("recorded_config") == summary.get("replay_config"):
        replay, replay_note = None, "replay with the current config: same as live"
    labels = session.labels()
    perf = session.perf() if show_perf else pd.DataFrame()
    actions = _action_colors(session)
    used_actions = [n for n in actions if n in set(session.interventions()["name"])]
    t0, t1 = session.start, session.end

    rows: list[tuple[str, object, float]] = [("situation", None, 0.95)]
    for need in needs:
        active = _ever_active(session, need)
        rows.append(("need" if active else "inactive", need, 1.5 if active else 0.3))
        if active and features and session.rule_features(need):
            rows.append(("features", need, 1.1))
    rows.append(("interventions", None, 0.55 + 0.75 * max(len(used_actions), 1)))
    if not perf.empty:
        rows.append(("perf", None, 0.9))

    fig, axes = plt.subplots(len(rows), 1, sharex=True, figsize=(13, sum(h for *_, h in rows) + 1.5),
                             gridspec_kw={"height_ratios": [h for *_, h in rows], "hspace": 0.55})
    axes = np.atleast_1d(axes)
    title = f"{session.name}  ·  {', '.join(session.maps) or 'no level'}  ·  {t1 - t0:.0f} s"
    if session.participant_id not in ("none", ""):
        title += f"  ·  {session.participant_id}"
    fig.suptitle(title, x=0.01, y=0.995, ha="left", va="top", fontsize=12, fontweight="bold")

    all_needs = session.needs()
    feats = session.features() if features else None

    for ax, (kind, need, _) in zip(axes, rows):
        ax.grid(axis="x", color=GRID, linewidth=0.6)
        ax.set_axisbelow(True)
        if kind == "situation":
            _situation(ax, session, labels, replay_note)
        elif kind == "need":
            _need(ax, session, need, all_needs, replay, labels)
        elif kind == "inactive":
            _inactive(ax, session, need)
        elif kind == "features":
            _features(ax, session, need, feats)
        elif kind == "interventions":
            _interventions(ax, session, actions, used_actions)
        elif kind == "perf":
            _perf(ax, perf)

    axes[-1].set_xlim(t0, t1)
    axes[-1].set_xlabel("session time (s)")

    legend = [Patch(facecolor=LEVEL_COLOR["Elevated"], alpha=LEVEL_ALPHA["Elevated"] * 2, label="need Elevated"),
              Patch(facecolor=LEVEL_COLOR["High"], alpha=LEVEL_ALPHA["High"] * 2, label="need High"),
              Line2D([], [], color=SCORE, lw=2, label="need score (smoothed)"),
              Line2D([], [], color=SCORE, lw=0.8, alpha=0.5, label="raw score")]
    if replay is not None:
        legend.append(Line2D([], [], color=REPLAY, lw=1.2, ls=(0, (3, 2)), label="replay with current config"))
    if not labels.empty:
        legend.append(Patch(facecolor="none", edgecolor=INK_2, hatch="////", label="labeled: should be raised"))
    height = fig.get_figheight()
    fig.legend(handles=legend, loc="upper left", ncol=len(legend), fontsize=8.5,
               bbox_to_anchor=(0.005, 1 - 0.36 / height))
    fig.subplots_adjust(left=0.075, right=0.94, top=1 - 0.85 / height, bottom=0.5 / height)
    return fig


def _situation(ax, session: Session, labels: pd.DataFrame, note: str = ""):
    _lane_title(ax, "What the player is doing", note)
    ax.set_ylim(0, 3)
    ax.set_yticks([2.5, 1.5, 0.5], ["context", "labels", "scenario"])
    ax.tick_params(axis="y", length=0)
    ax.spines["left"].set_visible(False)
    span = session.end - session.start
    for _, c in session.contexts().iterrows():
        color, alpha = CONTEXT_COLORS.get(c["value"], (MUTED, 0.25))
        ax.axvspan(c["start"], c["end"], 2.12 / 3, 2.88 / 3, color=color, alpha=alpha, lw=0)
        if c["end"] - c["start"] > 0.03 * span:
            ax.text(max(c["start"], session.start) + 0.003 * span, 2.5, _short(c["value"]), va="center",
                    fontsize=8, color=INK, clip_on=True)
    for _, lab in labels.iterrows():
        ax.axvspan(lab["from"], lab["to"], 1.12 / 3, 1.88 / 3, facecolor="none", edgecolor=INK_2, hatch="////", lw=0.6)
        ax.text(lab["from"] + 0.003 * span, 1.5, ", ".join(lab["needs"]) + (f": {lab['note']}" if lab["note"] else ""),
                va="center", fontsize=8, color=INK, clip_on=True,
                bbox=dict(facecolor=SURFACE, edgecolor="none", pad=1))
    phases = session.scenario_phases()
    if not phases.empty:
        started = phases[phases["event"] == "started"]
        times = started["t"].to_list()
        for i, (_, p) in enumerate(started.iterrows()):
            ax.axvline(p["t"], 0.04, 0.29, color=INK_2, lw=1)
            if i + 1 < len(times) and times[i + 1] - p["t"] < 0.04 * span:
                continue  # the next phase starts right after: label that one
            ax.text(max(p["t"], session.start) + 0.003 * span, 0.5, str(p["phase"]), va="center", fontsize=8,
                    color=INK_2, clip_on=True)
        for t in phases.loc[phases["event"] == "failed", "t"]:
            ax.axvline(t, 0.04, 0.29, color=LEVEL_COLOR["High"], lw=1.5)
            ax.text(t + 0.003 * span, 0.5, "player died", va="center", fontsize=7.5, color=INK_2, clip_on=True,
                    bbox=dict(facecolor=SURFACE, edgecolor="none", pad=0.5))
    for _, m in session.markers().iterrows():
        ax.axvline(m["t"], 0.04, 0.29, color=MUTED, lw=0.8, ls=":")


def _shade_levels(ax, intervals: pd.DataFrame, lo: float = 0.0, hi: float = 1.0, strength: float = 1.0):
    for _, iv in intervals.iterrows():
        if iv["value"] in LEVEL_COLOR:
            ax.axvspan(iv["start"], iv["end"], lo, hi, color=LEVEL_COLOR[iv["value"]],
                       alpha=min(LEVEL_ALPHA[iv["value"]] * strength, 1), lw=0)


def _need(ax, session: Session, need: str, all_needs: pd.DataFrame, replay, labels: pd.DataFrame):
    ax.set_ylim(0, 1.05)
    ax.set_yticks([0, 0.5, 1])
    _panel_label(ax, need)
    _shade_levels(ax, session.level_intervals(need))
    for _, lab in labels.iterrows():
        if need in lab["needs"]:
            ax.axvspan(lab["from"], lab["to"], 0.93, 1.0, facecolor="none", edgecolor=INK_2, hatch="////", lw=0.6)

    # thresholds: where a level is entered (dashed, named on the right) and left again (dotted)
    levels = session.level_settings(need)
    for level, enter, exit_ in [("Elevated", "elevated_enter", "elevated_exit"), ("High", "high_enter", "high_exit")]:
        if enter in levels:
            ax.axhline(levels[enter], color=MUTED, lw=0.7, ls="--")
            ax.text(1.004, levels[enter], f"{level} ≥ {levels[enter]:g}", transform=ax.get_yaxis_transform(),
                    va="center", fontsize=7, color=INK_2)
        if exit_ in levels:
            ax.axhline(levels[exit_], color=MUTED, lw=0.6, ls=":", alpha=0.8)
            ax.text(1.004, levels[exit_], f"leave < {levels[exit_]:g}", transform=ax.get_yaxis_transform(),
                    va="center", fontsize=6.5, color=MUTED)

    d = all_needs[all_needs["need"] == need]
    if not d.empty:
        t, raw = _step(d, session.end, "raw")
        ax.step(t, raw, where="post", color=SCORE, lw=0.8, alpha=0.5)
        t, score = _step(d, session.end, "score")
        ax.step(t, score, where="post", color=SCORE, lw=2)
    if replay is not None and not replay.empty:
        r = replay[replay["need"] == need]
        ax.plot(r["t"], r["score"], color=REPLAY, lw=1.2, ls=(0, (3, 2)))
        # the replay's level as a strip along the bottom, so a changed config shows where it differs
        for level, color in LEVEL_COLOR.items():
            on = (r["level"] == level).to_numpy()
            ax.fill_between(r["t"], 0, 0.06, where=on, step="post", color=color, alpha=0.8, lw=0)


def _inactive(ax, session: Session, need: str):
    ax.set_ylim(0, 1)
    ax.set_yticks([])
    ax.spines["left"].set_visible(False)
    contexts = ", ".join(_short(c) for c in session.required_contexts(need))
    note = f"not active in this session (counts only in {contexts})" if contexts else "not active in this session"
    name = ax.text(0.004, 0.5, need, transform=ax.transAxes, va="center", fontsize=9.5, fontweight="bold",
                   color=MUTED)
    ax.annotate(note, xy=(1, 0.5), xycoords=name, xytext=(8, 0), textcoords="offset points", va="center",
                fontsize=8, color=MUTED)


def _features(ax, session: Session, need: str, feats: pd.DataFrame):
    names = session.rule_features(need)
    ax.set_ylim(0, 1.05)
    ax.set_yticks([0, 1])
    for i, name in enumerate(names):
        d = feats[feats["name"] == name]
        if d.empty:
            continue
        t, v = _step(d, session.end, "value")
        ax.step(t, v, where="post", lw=1.2, color=SERIES[(i + 1) % len(SERIES)], label=name.split(".", 1)[-1])
    ax.legend(loc="upper left", ncol=min(len(names), 5), fontsize=7, handlelength=1.2, borderaxespad=0.1,
              title=f"{need}: features its rules use", title_fontsize=7, alignment="left")


def _interventions(ax, session: Session, actions: dict[str, str], names: list[str]):
    _lane_title(ax, "Companion interventions",
                "bar = help running   ·   thin line = up to 10 s after it, while its effect is measured (shorter when "
                "the next help or a context change came first)   ·   text below = the need's level at selection → after",
                below=True)
    ivs = session.interventions()
    n = max(len(names), 1)
    ax.set_ylim(-0.6, n - 0.4)
    ax.set_yticks([n - 1 - i for i in range(len(names))], [_short(x) for x in names])
    ax.tick_params(axis="y", length=0)
    ax.spines["left"].set_visible(False)
    for _, iv in ivs.iterrows():
        if iv["name"] not in names:
            continue
        y = n - 1 - names.index(iv["name"])
        color = actions[iv["name"]]
        start = iv["selected"] if pd.notna(iv["selected"]) else iv["dispatched"]
        if pd.isna(start):
            continue
        end = iv["end"] if pd.notna(iv["end"]) else session.end
        ax.barh(y, end - start, left=start, height=0.26, color=color, lw=0)
        if pd.notna(iv["after_duration"]) and iv["after_duration"] > 0:
            after_end = end + iv["after_duration"]
            ax.plot([end, after_end], [y, y], color=color, lw=1.2, alpha=0.6, solid_capstyle="butt")
            ax.plot([after_end], [y], marker="|", ms=7, color=color, alpha=0.8)

        how = iv["detail"] if isinstance(iv["detail"], str) and iv["detail"] else iv["end_phase"]
        if pd.isna(iv["end"]):
            how = "still running at the end"
        ax.text(start, y + 0.2, f"#{iv['id']}  {how}" if isinstance(how, str) else f"#{iv['id']}",
                va="bottom", fontsize=7.5, color=INK, clip_on=True)
        if isinstance(iv["level_baseline"], str):
            after = iv["level_after"] if isinstance(iv["level_after"], str) else iv["level_end"]
            text = f"{iv['need']}: {iv['level_baseline']} → {after}"
            ax.text(start, y - 0.2, text, va="top", fontsize=7, clip_on=True,
                    color=INK if after == "Normal" else INK_2,
                    fontweight="bold" if after == "Normal" else "normal")


def _perf(ax, perf: pd.DataFrame):
    _lane_title(ax, "Pipeline cost per update (µs)", "per 5 s window")
    ax.step(perf["t"], perf["update_us_mean"], where="pre", color=SCORE, lw=1.5, label="mean")
    ax.step(perf["t"], perf["update_us_p95"], where="pre", color=SCORE, lw=0.8, alpha=0.5, label="p95")
    ax.set_ylim(0, max(perf["update_us_p95"].max() * 1.2, 1))
    ax.legend(loc="upper left", fontsize=7, ncol=2)


# ------------------------------------------------------------ measurements

LATENCY_PARTS = [("pipeline_ms", "pipeline", "#2a78d6"), ("handoff_ms", "handoff", "#6250d6"),
                 ("render_ms", "render", "#1baf7a")]
STAGES = ["Telemetry", "Feature", "Context", "Need", "Selection", "Behavior", "Recording"]


def latency_figure(latency: pd.DataFrame):
    """Selection → screen per intervention, split into pipeline, handoff and render (stacked)."""
    d = latency[latency["incomplete"].fillna("") == ""].reset_index(drop=True)
    fig, ax = plt.subplots(figsize=(max(6, 0.28 * len(d) + 2), 3.6))
    bottom = np.zeros(len(d))
    for col, label, color in LATENCY_PARTS:
        v = d[col].fillna(0).to_numpy()
        ax.bar(range(len(d)), v, bottom=bottom, color=color, width=0.8, label=label, edgecolor=SURFACE, lw=1)
        bottom += v
    ax.set_xticks(range(len(d)), [f"{s[-8:]}#{i}" for s, i in zip(d["session"], d["id"])], rotation=90, fontsize=6)
    ax.set_ylabel("selection → screen (ms)")
    ax.set_title(f"Reaction per intervention ({len(d)} reached the screen)", loc="left")
    ax.grid(axis="y", color=GRID, lw=0.6)
    ax.set_axisbelow(True)
    ax.legend(loc="upper left", ncol=3)
    fig.tight_layout()
    return fig


def perf_figure(summary: pd.DataFrame):
    """Mean cost per update and session, stacked by stage."""
    fig, ax = plt.subplots(figsize=(max(6, 0.45 * len(summary) + 2), 3.6))
    bottom = np.zeros(len(summary))
    colors = SERIES + ["#8a8984"]
    for i, stage in enumerate(STAGES):
        col = f"{stage}_us_mean"
        if col not in summary:
            continue
        v = summary[col].fillna(0).to_numpy()
        ax.bar(range(len(summary)), v, bottom=bottom, color=colors[i], width=0.8, label=stage, edgecolor=SURFACE, lw=1)
        bottom += v
    ax.set_xticks(range(len(summary)), list(summary["session"]), rotation=60, ha="right", fontsize=7)
    ax.set_ylabel("mean cost per update (µs)")
    ax.set_title("Pipeline cost per update, by stage", loc="left")
    ax.grid(axis="y", color=GRID, lw=0.6)
    ax.set_axisbelow(True)
    ax.legend(loc="upper left", ncol=4, fontsize=7)
    fig.tight_layout()
    return fig

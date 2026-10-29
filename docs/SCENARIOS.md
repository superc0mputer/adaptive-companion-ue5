# Test scenarios

Repeatable situations for testing the framework. A scenario runs on its own in a level, without a
script or test harness, and writes its phases to the session log. The phases are ground truth for
evaluating the need estimation, so they are **only logged and never reported to the pipeline**:
the estimation has to find out from telemetry alone when the player is under pressure.

## Combat scenario

`Content/Scenarios/Lvl_CombatScenario` (#11). Open it and press Play.

| Phase | What happens |
|---|---|
| `Start` | 5 s countdown after the level starts |
| `WarmUp` | 1 enemy, 10 m in front of the player |
| `BuildUp` | 2 enemies at once, in front |
| `Pressure` | 4 enemies at once, two on each side of the arena (the hard part) |
| `End` | All waves cleared |

Each wave starts 4 s after the previous one is cleared. Waves end by clearing, not on a timer, so
every phase has a clear start and end in the log no matter how fast the player is.

**Player death:** the current wave is lost. Its enemies are removed, the player respawns at the
arena start, and the wave starts again 4 s later. Death is itself the strongest struggle signal,
so a lost wave is logged (`failed`, then `restarted`) instead of being glossed over.

**Repeatability:** the scenario plays the same way on every run:

- One PlayerStart. The combat game mode picks a random one if a level has several.
- Fixed spawn points and delays, defined in C++ on `ACombatScenario`.
- The global random stream is reseeded with `Seed + wave index` at every wave start, also when a
  wave is replayed.

"The same" means the same waves, spawn points and delays. The enemies' moment-to-moment behavior
still varies a little between runs, even with identical player input ([example runs](#example-runs)),
and different players naturally take different times.

### Layout

The level started as a copy of the engine's empty level template (sky, lights and an 80 × 80 m
floor). To rebuild it, add:

| Actor | Where (cm) | Notes |
|---|---|---|
| 4 walls (`/Game/LevelPrototyping/Meshes/SM_Cube`, pivot at a corner) | N: (1500, −1600, 0) scale (1, 32, 3) · S: (−1600, −1600, 0) scale (1, 32, 3) · E: (−1500, 1500, 0) scale (30, 1, 3) · W: (−1500, −1600, 0) scale (30, 1, 3) | 30 × 30 m inside, 3 m high |
| PlayerStart | (−1000, 0, 92), facing +X | The only one in the level |
| `CombatScenario` | (0, 0, 0) | *Enemy Class* = `BP_CombatEnemy` |
| World Settings | | *GameMode Override* = `BP_CombatGameMode` |

The navmesh is generated at runtime (`UCompanionNavBoundsSubsystem`), so the level has no
NavMeshBoundsVolume.

### Session log

Every phase change is a `scenario` record:

| Field | Value |
|---|---|
| `scenario` | `Combat` |
| `phase` | `Start`, `WarmUp`, `BuildUp`, `Pressure`, `End` |
| `event` | `started`, `enemy_killed`, `cleared`, `failed` (player died), `restarted` |
| `wave` | Wave index; −1 during `Start`, the number of waves at `End` |
| `enemies_left` | Living enemies of the wave after the event |

The overlay shows the current phase in its Context section (`Scenario.Combat`).

To split a session into phases in Python:

```python
sc = data[data.type == "scenario"]
sc = sc.join(pd.DataFrame(sc.payload.tolist(), index=sc.index))
starts = sc[sc.event.isin(["started", "restarted"])][["t", "phase"]]
data = pd.merge_asof(data.sort_values("t"), starts, on="t")  # adds a "phase" column
```

### Example runs

Three scripted `-game` runs. The scripted player stands still and kills the enemies of `WarmUp`
after 3 s and of `BuildUp` after 3 and 5 s. It leaves the first `Pressure` attempt alone, so the
four enemies kill it, then clears the restarted wave after 3–6 s. Times in s since `Start`:

| Run | `WarmUp` | `BuildUp` | `Pressure` | Player dies | `Pressure` restarted | `End` |
|---|---|---|---|---|---|---|
| A | 5.0 | 12.1 | 21.2 | 40.3 | 47.4 | 53.6 |
| B | 5.0 | 12.0 | 21.1 | 35.4 | 42.5 | 48.5 |
| C | 5.0 | 12.1 | 21.2 | 39.8 | 46.8 | 52.8 |

- **Same structure every run:** same phase order, same spawn points, waves 4 s after clearing, the
  restart 7 s after the death (3 s respawn, 4 s wave delay).
- **Clear pressure:** before `Pressure` the player takes no damage. In `Pressure`, 4 enemies are
  within 15 m and the player dies 14–19 s after the wave starts, without fighting back.
- **Enemy behavior still varies:** with the same (idle) player, the first hit lands 3.6–8.0 s and
  the death 14.3–19.1 s into `Pressure`. The template's enemy StateTree picks behaviors, delays
  and EQS results at random, from the global random stream that other engine systems also draw
  from depending on frame timing. Reseeding narrows this but can't remove it without replacing
  Epic's enemy AI. In run C the camera was turned toward a flank for the screenshot below.

![Pressure: four enemies attack the player from both sides; the overlay shows the phase](images/combat-scenario.png)

To start at a later wave while testing, set *First Wave* on the `CombatScenario` actor (Testing
category), e.g. 2 for `Pressure`. Waves before it are skipped and not logged.

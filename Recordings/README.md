# Recordings

Labeled session logs from scripted `-game` runs, for replays (`-run=AdaptiveReplay`). Each
`<name>.jsonl` is a complete session log with input records; `<name>.labels.json` says which needs
should be Elevated when. What each one shows and how they replay: [docs/REPLAY.md](../docs/REPLAY.md#example-recordings).

```bash
UnrealEditor-Cmd AdaptiveCompanion/AdaptiveCompanion.uproject -run=AdaptiveReplay -Log=$PWD/Recordings/maze-hint-ignored.jsonl -unattended -nullrhi
```

The results (`*.replay.json`, `*.replay.jsonl`) are written next to the recording and ignored by git.

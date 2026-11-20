"""Runs the analysis end to end on the example recordings: python -m unittest analysis/test_analysis.py"""

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

import pandas as pd

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import aclog  # noqa: E402
import analyze  # noqa: E402

RECORDINGS = HERE.parent / "Recordings"


class AnalysisTest(unittest.TestCase):
    def setUp(self):
        self.out = Path(tempfile.mkdtemp())
        # the replay output is ignored by git, so the test works on copies without it
        self.logs = self.out / "logs"
        self.logs.mkdir()
        for f in RECORDINGS.glob("*"):
            if f.suffix in (".jsonl", ".json") and ".replay." not in f.name:
                shutil.copy(f, self.logs / f.name)

    def tearDown(self):
        shutil.rmtree(self.out)

    def test_loads_a_recording(self):
        s = aclog.load(self.logs / "arena-pressure-wave.jsonl")
        self.assertEqual(s.need_names, ["Combat", "Threat", "Navigation", "Disorientation"])
        self.assertEqual(s.maps, ["Lvl_CombatScenario"])
        ivs = s.interventions()
        self.assertEqual(list(ivs["name"]), ["Action.ThreatCallout", "Action.DrawAggro",
                                             "Action.ThreatCallout", "Action.ThreatCallout"])
        self.assertEqual(ivs["detail"].iloc[0], "threat in view")
        self.assertEqual(ivs["need"].iloc[0], "Threat")
        levels = s.level_intervals("Threat")
        self.assertAlmostEqual(levels["end"].iloc[-1], s.end)
        self.assertAlmostEqual(aclog.overlap(levels, s.start, s.end), s.end - s.start, places=6)
        labels = s.labels()
        self.assertEqual(labels["needs"].iloc[0], ["Threat"])

    def test_labels_from_markers_match_the_labels_file(self):
        s = aclog.load(self.logs / "maze-hint-ignored.jsonl")
        from_file = s.labels()
        s.labels_path = None
        from_markers = s.labels()
        self.assertEqual(len(from_file), len(from_markers))
        self.assertAlmostEqual(from_file["from"].iloc[0], from_markers["from"].iloc[0], places=1)
        self.assertEqual(from_file["needs"].iloc[0], from_markers["needs"].iloc[0])

    def test_runs_end_to_end(self):
        out = self.out / "result"
        self.assertEqual(analyze.main([str(self.logs), "--out", str(out), "--features"]), 0)
        for name in ["sessions.csv", "needs.csv", "interventions.csv", "report.md"]:
            self.assertTrue((out / name).exists(), name)
        for rec in ["arena-pressure-wave", "maze-hint-followed", "maze-hint-ignored"]:
            self.assertTrue((out / "sessions" / f"{rec}.png").exists(), rec)
        needs = pd.read_csv(out / "needs.csv")
        threat = needs[(needs.session == "arena-pressure-wave") & (needs.need == "Threat")].iloc[0]
        self.assertGreater(threat.raised_in_label_s, 25)
        self.assertEqual(threat.raised_outside_label_s, 0)


if __name__ == "__main__":
    unittest.main()

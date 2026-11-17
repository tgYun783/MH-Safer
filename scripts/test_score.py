import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from score import main, score


BASE = "0x1000"


def addresses():
    return [
        {
            "ns": 100,
            "event": "state_alloc",
            "addr": BASE,
            "size": 16,
            "fields": [
                {"name": "timer", "offset": 0, "size": 4},
                {"name": "cells_opened", "offset": 4, "size": 4},
                {"name": "game_state", "offset": 8, "size": 4},
            ],
        },
        {"ns": 200, "event": "field_watch", "addr": BASE,
         "fields": [{"name": "mine_field", "offset": 12, "size": 4}]},
        {"ns": 9_000_000, "event": "state_free", "addr": BASE},
    ]


def label(ns, name="timer", addr=BASE, hidden=False):
    return {
        "inject_ns": ns,
        "name": name,
        "addr": addr,
        "method": "proc",
        "scenario": "T-" + name,
        "from": None,
        "to": None,
        "hidden": hidden,
    }


def alert(ns, name="timer", kind="behavior"):
    return {
        "detect_ns": ns,
        "name": name,
        "kind": kind,
        "rule": None,
        "expected": None,
        "actual": None,
    }


def fusion_fixture():
    logs = addresses()
    labels = [
        label(1_000_000_000),
        label(3_000_000_000, "cells_opened", "0x1004"),
        label(5_000_000_000, "game_state", "0x1008"),
        label(7_000_000_000, hidden=True),
        label(10_000_000_000),
    ]
    alerts = [
        alert(1_100_000_000),
        alert(1_120_000_000, kind="prediction"),
        alert(3_100_000_000, "cells_opened", "prediction"),
        alert(5_250_000_000, "game_state"),
        alert(5_150_000_000, "game_state", "prediction"),
        alert(7_100_000_000),
        alert(8_000_000_000),
        alert(8_500_000_000, "game_state"),
        alert(8_600_000_000, "game_state", "prediction"),
    ]
    logs[-1]["ns"] = 9_000_000_000
    return (
        labels,
        alerts,
        logs,
    )


class ScoreTests(unittest.TestCase):
    def test_all_detected_and_multiple_matching_alerts_count_once(self):
        result = score(
            [label(300)],
            [alert(300), alert(400, kind="prediction")],
            addresses(),
            window_ms=1,
            legal_scopes=100,
            elapsed_seconds=600,
        )
        self.assertEqual(result["counts"]["tp"], 1)
        self.assertEqual(result["counts"]["fn"], 0)
        self.assertEqual(result["counts"]["fp"], 0)
        self.assertEqual(result["latency_ns"]["median"], 0.0)

    def test_missed_stale_and_hidden_labels_are_separated(self):
        result = score(
            [label(300), label(10_000_000), label(2_000_000, hidden=True)],
            [alert(450)],
            addresses(),
            window_ms=1,
        )
        self.assertEqual(result["counts"]["tp"], 1)
        self.assertEqual(result["counts"]["fn"], 0)
        self.assertEqual(result["counts"]["stale"], 1)
        self.assertEqual(result["counts"]["hidden_labels"], 1)
        self.assertEqual(result["counts"]["hidden_tp"], 0)

    def test_partial_miss_counts_one_false_negative(self):
        result = score(
            [label(300), label(2_000_000)],
            [alert(450)],
            addresses(),
            window_ms=1,
        )
        self.assertEqual(result["counts"]["tp"], 1)
        self.assertEqual(result["counts"]["fn"], 1)

    def test_boundary_alert_is_inclusive_and_unmatched_alert_is_fp(self):
        result = score(
            [label(300)],
            [alert(1_000_300), alert(1_000_301)],
            addresses(),
            window_ms=1,
        )
        self.assertEqual(result["counts"]["tp"], 1)
        self.assertEqual(result["counts"]["fp"], 1)

    def test_stale_before_field_watch_is_excluded(self):
        result = score(
            [label(150, "mine_field", "0x100c")],
            [],
            addresses(),
        )
        self.assertEqual(result["counts"]["stale"], 1)

    def test_fusion_comparison_uses_same_labels_and_alerts(self):
        labels, alerts, addr_log = fusion_fixture()
        result = score(labels, alerts, addr_log, legal_scopes=100, elapsed_seconds=600)
        self.assertEqual(
            {name: (value["tp"], value["fn"], value["fp"])
             for name, value in result["fusion_comparison"].items()},
            {
                "OR": (3, 0, 4),
                "AND": (1, 2, 2),
                "behavior_only": (1, 2, 3),
                "prediction_only": (3, 0, 1),
            },
        )
        self.assertEqual(result["counts"]["stale"], 1)
        self.assertEqual(result["counts"]["hidden_tp"], 1)

    def test_consistency_alert_matches_forged_win_label(self):
        # 일관성 alert name = when.variable(game_state)이라 V-game_state 라벨과 묶인다 (interface.md §7)
        result = score(
            [label(300, "game_state", "0x1008")],
            [{**alert(400, "game_state", "prediction"),
              "rule": "won_requires_all_safe_cells_open", "expected": "0x47", "actual": "0x5"}],
            addresses(),
            window_ms=1,
        )
        self.assertEqual(result["counts"]["tp"], 1)
        self.assertEqual(result["counts"]["fn"], 0)
        self.assertEqual(result["counts"]["fp"], 0)

    def test_cli_writes_score_json_and_reads_scope_denominator(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "labels.jsonl").write_text(json.dumps(label(300)) + "\n", encoding="utf-8")
            (root / "alerts.jsonl").write_text(json.dumps(alert(450)) + "\n", encoding="utf-8")
            (root / "addr.jsonl").write_text(
                "".join(json.dumps(item) + "\n" for item in addresses()), encoding="utf-8"
            )
            (root / "stats.json").write_text(
                json.dumps({"mtd_end_count": 100, "elapsed_sec": 600}), encoding="utf-8"
            )
            output = root / "score.json"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                result = main([
                    "--labels", str(root / "labels.jsonl"),
                    "--alerts", str(root / "alerts.jsonl"),
                    "--addr-log", str(root / "addr.jsonl"),
                    "--stats", str(root / "stats.json"),
                    "--output", str(output),
                ])
            self.assertEqual(result, 0)
            saved = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(saved["counts"]["tp"], 1)
            self.assertEqual(saved["rates"]["false_positive_rate"], 0.0)
            self.assertIn("strategy", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""S4-01 실험 요약: scripts/run-experiment.sh 결과 폴더 → Markdown 표.

  python3 -I scripts/summarize_experiment.py results/2026-11-24_tamper-T_*

- 시나리오 표: 시나리오(칸)별로 시드를 합친 TP·FN·stale·탐지율. 탐지율 = TP / (TP + FN) (docs/experiment.md §5).
  TP·FN·stale은 score_<시나리오>.json(그 시나리오 라벨만 채점)에서 가져온다.
- 지연: score.json에 원시 지연이 없어서 시드별 중앙값·p95·최대를 그대로 모으고, 표에는
  "시드별 p95 중 최댓값"과 "최대"를 낸다(합친 p95가 아님 — 원시 지연이 채점기에 생기면 바꾼다).
- 실행 표: 시드별 오탐(FP)·오탐률은 score.json(실행 전체)에서 가져온다.
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _ms(ns: float | None) -> str:
    return "—" if ns is None else f"{ns / 1e6:.1f}"


def _pct(x: float | None) -> str:
    return "—" if x is None else f"{x * 100:.1f}%"


def main(dirs: list[str]) -> int:
    if not dirs:
        print(__doc__, file=sys.stderr)
        return 2
    runs = []
    for d in sorted(Path(p) for p in dirs):
        meta = _load(d / "meta.json")
        stats = _load(d / "stats.json") if (d / "stats.json").exists() else {}
        score = _load(d / "score.json")
        per = {}
        for sc in meta.get("scenarios", []):
            sp = d / f"score_{sc}.json"
            inj = d / f"injector_{sc}.json"
            per[sc] = (_load(sp) if sp.exists() else None, _load(inj) if inj.exists() else {})
        runs.append((d, meta, stats, score, per))

    exp = {r[1]["experiment"] for r in runs}
    print(f"실험: {', '.join(sorted(exp))} · 폴더 {len(runs)}개 · 커밋 {runs[0][1]['commit'][:7]}"
          f"{' (dirty)' if any(r[1]['dirty'] for r in runs) else ''}\n")

    print("| 폴더 | 게임 종료 코드 | 주입기 종료 코드 | 판(승/패) | 라벨 | FP | `mtd_end` 수 | 오탐률 | 10분당 FP |")
    print("|---|---|---|---|---|---|---|---|---|")
    for d, meta, stats, score, per in runs:
        labels = sum(1 for line in (d / "labels.jsonl").read_text().splitlines() if line.strip()) \
            if (d / "labels.jsonl").exists() else 0
        inj = meta.get("injector_exit_codes", {})
        inj_s = "—" if not inj else ("0" if all(v == 0 for v in inj.values())
                                     else ", ".join(f"{k}={v}" for k, v in inj.items() if v))
        rates = score["rates"]
        per10 = rates["false_positives_per_10min"]
        per10_s = "—" if per10 is None else f"{per10:.2f}"
        print(f"| {d.name} | {meta['exit_code']} | {inj_s} | {stats.get('games_won', '?')}/{stats.get('games_lost', '?')} "
              f"| {labels} | {score['counts']['fp']} | {stats.get('mtd_end_count', '?')} "
              f"| {_pct(rates['false_positive_rate'])} | "
              f"{per10_s} |")

    total_fp = sum(r[3]["counts"]["fp"] for r in runs)
    total_scopes = sum(r[2].get("mtd_end_count", 0) for r in runs)
    print(f"\n합계: FP {total_fp} / `mtd_end` {total_scopes} → 오탐률 "
          f"{_pct(total_fp / total_scopes) if total_scopes else '—'}")

    agg: dict[str, dict] = defaultdict(lambda: {"tp": 0, "fn": 0, "stale": 0, "hidden": 0, "hidden_tp": 0,
                                               "p95": [], "max": [], "median": [], "missing": 0,
                                               "behavior_tp": 0, "prediction_tp": 0})
    order: list[str] = []
    for _, meta, _, _, per in runs:
        for sc, (sj, _) in per.items():
            if sc not in order:
                order.append(sc)
            a = agg[sc]
            if sj is None:
                a["missing"] += 1
                continue
            c = sj["counts"]
            for k in ("tp", "fn", "stale"):
                a[k] += c[k]
            a["hidden"] += c["hidden_labels"]
            a["hidden_tp"] += c["hidden_tp"]
            a["behavior_tp"] += sj["by_kind"]["behavior"]["tp"] + sj["by_kind"]["behavior"]["hidden_tp"]
            a["prediction_tp"] += sj["by_kind"]["prediction"]["tp"] + sj["by_kind"]["prediction"]["hidden_tp"]
            for k in ("median", "p95", "max"):
                if sj["latency_ns"][k] is not None:
                    a[k].append(sj["latency_ns"][k])
    if not order:
        return 0

    print("\n| 시나리오 | 유효 라벨(stale 제외) | stale | TP | FN | 탐지율 | 은닉형 탐지 | behavior / prediction 탐지 "
          "| 지연 중앙값(시드별 중앙값의 중앙값, ms) | 시드별 p95 최댓값(ms) | 최대(ms) |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for sc in order:
        a = agg[sc]
        valid = a["tp"] + a["fn"] + a["hidden"]
        rate = a["tp"] / (a["tp"] + a["fn"]) if a["tp"] + a["fn"] else None
        hid = f"{a['hidden_tp']}/{a['hidden']}" if a["hidden"] else "—"
        med = sorted(a["median"])[len(a["median"]) // 2] if a["median"] else None
        note = f" (채점 파일 없음 {a['missing']})" if a["missing"] else ""
        print(f"| `{sc}`{note} | {valid} | {a['stale']} | {a['tp']} | {a['fn']} | {_pct(rate)} | {hid} "
              f"| {a['behavior_tp']} / {a['prediction_tp']} | {_ms(med)} "
              f"| {_ms(max(a['p95']) if a['p95'] else None)} | {_ms(max(a['max']) if a['max'] else None)} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

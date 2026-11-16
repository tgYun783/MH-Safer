#!/usr/bin/env python3
"""S3-05 정상 플레이 결과 요약: results/<날짜>_normal_<시드>/ → 오탐률 표 (Markdown).

정상 플레이에서는 alert가 모두 거짓 양성(FP)이다(docs/experiment.md §3).
오탐률 = FP / mtd_end 호출 수, 10분당 FP 건수를 함께 낸다(§7, 11-16 PM 결정).
채점기(S3-21)가 나오기 전 임시 계산이다. 채점기 결과와 다르면 채점기를 따른다.

  python3 -I scripts/summarize_normal.py results/2026-11-16_normal_*   > docs/results/...md
"""
import json
import statistics
import sys
from pathlib import Path


def game_lengths(addr_path):
    """state_alloc ~ state_free 간격(초). 마지막 판(실행 종료로 해제)도 포함."""
    alloc = {}
    lengths = []
    order_errors = 0
    live = None
    for line in addr_path.read_text().splitlines():
        if not line.strip():
            continue
        e = json.loads(line)
        if e["event"] == "state_alloc":
            if live is not None:
                order_errors += 1
            live = e["addr"]
            alloc[e["addr"]] = e["ns"]
        elif e["event"] == "field_watch":
            if e["addr"] != live:
                order_errors += 1
        elif e["event"] == "state_free":
            if e["addr"] != live:
                order_errors += 1
            else:
                lengths.append((e["ns"] - alloc[e["addr"]]) / 1e9)
            live = None
    return lengths, order_errors


def main(dirs):
    rows = []
    for d in sorted(Path(p) for p in dirs):
        meta = json.loads((d / "meta.json").read_text())
        stats = json.loads((d / "stats.json").read_text())
        alerts = [l for l in (d / "alerts.jsonl").read_text().splitlines() if l.strip()]
        lengths, order_errors = game_lengths(d / "addr.jsonl")
        stderr = (d / "stderr.txt").read_text().strip()
        rows.append((d, meta, stats, alerts, lengths, order_errors, stderr))

    total_fp = sum(len(r[3]) for r in rows)
    total_scopes = sum(r[2]["mtd_end_count"] for r in rows)
    total_sec = sum(r[2]["elapsed_sec"] for r in rows)
    all_lengths = [x for r in rows for x in r[4]]
    m = rows[0][1]

    print("| 시드 | 종료 코드 | 판(승/패) | FP(alert) | `mtd_end` 수 | 오탐률 | 10분당 FP | 판 길이 중앙값 | 주소 로그 순서 위반 | stderr |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for d, meta, stats, alerts, lengths, order_errors, stderr in rows:
        fp = len(alerts)
        rate = fp / stats["mtd_end_count"] if stats["mtd_end_count"] else float("nan")
        per10 = fp / stats["elapsed_sec"] * 600 if stats["elapsed_sec"] else float("nan")
        med = f"{statistics.median(lengths):.1f}초" if lengths else "—"
        print(f"| {meta['seed']} | {meta['exit_code']} | {stats['games_won']}/{stats['games_lost']} | {fp} | "
              f"{stats['mtd_end_count']:,} | {rate:.4%} | {per10:.2f} | {med} | {order_errors} | "
              f"{'비어 있음' if not stderr else '있음'} |")
    rate = total_fp / total_scopes if total_scopes else float("nan")
    print(f"| **합계** | — | {sum(r[2]['games_won'] for r in rows)}/{sum(r[2]['games_lost'] for r in rows)} | "
          f"**{total_fp}** | {total_scopes:,} | **{rate:.4%}** | {total_fp / total_sec * 600:.2f} | "
          f"{statistics.median(all_lengths):.1f}초 | {sum(r[5] for r in rows)} | — |")
    print()
    print(f"조건: `{m['difficulty']}`, `--speed-ms {m['speed_ms']}`, {m['duration_sec']}초 × {len(rows)}회 "
          f"**동시 실행**(concurrent_runs={m['concurrent_runs']}), 커밋 `{m['commit'][:7]}`"
          f"{' (dirty)' if m['dirty'] else ''}, {m['start_utc']} ~ {m['end_utc']} UTC. "
          f"호스트 {m['host']['cpu']} {m['host']['cores']}코어, {m['host']['os']}, {m['host']['virtualization']} "
          f"{m['host']['docker']}, 컨테이너 `{m['container']['image']}` ({m['container']['compiler']}). "
          f"libmtd T={m['period_ms']}ms, mprotect 끔, 규칙 `{m['rules']}`.")
    if total_fp == 0:
        print()
        print("FP가 0건이라 오탐률은 0%다. 이 표본에서 오탐률의 95% 상한(3/n 규칙)은 "
              f"{3 / total_scopes:.4%}이다(n = `mtd_end` {total_scopes:,}회).")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])

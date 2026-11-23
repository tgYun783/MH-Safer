#!/usr/bin/env python3
"""T4 2048 자동 플레이 (S4-04): 무작위 wasd를 파이프로 넣고, 게임이 끝나면 새 프로세스로 다시 시작한다.

  python3 -I examples/2048/autoplay.py --bin build/examples/2048 --rules examples/2048/rules.json \
      --duration-sec 600 --seed 1 --alerts alerts.jsonl --stats stats.json

- libmtd는 콜백 없이 초기화되어(mtd.patch) alert를 stderr에 docs/interface.md §2 JSONL로 쓴다.
  stderr에서 `{`로 시작하는 줄만 alerts 파일에 모으고, 나머지(경고)는 stats의 `stderr_other`에 센다.
- 크래시 = 시그널로 끝났거나 0이 아닌 종료 코드. 시간이 다 돼서 우리가 끝낸 마지막 판은 크래시로 세지 않는다.
- 2048은 한 수마다 150ms 쉬므로(원본) 입력 간격 기본값은 그보다 조금 긴 160ms.
"""
from __future__ import annotations

import argparse
import json
import os
import random
import subprocess
import sys
import threading
import time


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--rules", required=True)
    ap.add_argument("--duration-sec", type=float, default=600)
    ap.add_argument("--interval-ms", type=int, default=160)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--alerts", required=True)
    ap.add_argument("--stats", required=True)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    env = dict(os.environ, MTD_RULES=a.rules)
    deadline = time.monotonic() + a.duration_sec
    stats = {"program": "2048", "seed": a.seed, "duration_sec": a.duration_sec, "interval_ms": a.interval_ms,
             "games": 0, "games_over": 0, "moves_sent": 0, "crashes": 0, "exit_codes": {}, "alerts": 0,
             "stderr_other": 0, "stderr_other_samples": []}
    lock = threading.Lock()
    started = time.monotonic()

    with open(a.alerts, "w", encoding="utf-8") as alerts:
        def drain(stream) -> None:
            for raw in stream:
                line = raw.decode("utf-8", "replace").rstrip("\n")
                with lock:
                    if line.startswith("{"):
                        alerts.write(line + "\n")
                        stats["alerts"] += 1
                    elif line.strip():
                        stats["stderr_other"] += 1
                        if len(stats["stderr_other_samples"]) < 5:
                            stats["stderr_other_samples"].append(line)

        while time.monotonic() < deadline:
            p = subprocess.Popen([a.bin], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.PIPE, env=env)
            t = threading.Thread(target=drain, args=(p.stderr,), daemon=True)
            t.start()
            stats["games"] += 1
            killed = False
            while p.poll() is None:
                if time.monotonic() >= deadline:
                    p.terminate()  # 원본의 SIGINT 처리기는 exit(signum)을 부르므로 SIGTERM으로 끝낸다
                    killed = True
                    break
                try:
                    p.stdin.write(rng.choice(b"wasd").to_bytes(1, "little"))
                    p.stdin.flush()
                    stats["moves_sent"] += 1
                except BrokenPipeError:
                    break
                time.sleep(a.interval_ms / 1000)
            code = p.wait()
            t.join()
            try:
                p.stdin.close()
            except BrokenPipeError:
                pass
            if not killed:
                stats["exit_codes"][str(code)] = stats["exit_codes"].get(str(code), 0) + 1
                if code == 0:
                    stats["games_over"] += 1
                else:
                    stats["crashes"] += 1

    stats["elapsed_sec"] = round(time.monotonic() - started, 3)
    with open(a.stats, "w", encoding="utf-8") as f:
        json.dump(stats, f, ensure_ascii=False, indent=1)
    print(json.dumps({k: stats[k] for k in ("games", "games_over", "crashes", "alerts", "moves_sent")}))
    return 1 if stats["crashes"] else 0


if __name__ == "__main__":
    sys.exit(main())

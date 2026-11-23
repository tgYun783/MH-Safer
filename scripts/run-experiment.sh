#!/usr/bin/env bash
# S4-01 실행 흐름: 게임(+주입기) 실행 → results/<run>/ (docs/experiment.md §6) → 채점(scripts/score.py).
# 개발 컨테이너(scripts/dev.sh, SYS_PTRACE)에서 Release 빌드 후 시드별 실행을 동시에 돌린다.
#
#   scripts/run-experiment.sh normal          # 정상 플레이 (§3), 시드 1~5 × 600초
#   scripts/run-experiment.sh T               # 타이밍 위반형 (§4), 유형 하나 = 실행 하나 (§4.2)
#   scripts/run-experiment.sh V
#   scripts/run-experiment.sh H
#   SEEDS="1" DURATION=60 COUNT=5 scripts/run-experiment.sh T      # 짧은 확인
#   SCENARIOS="T-timer" scripts/run-experiment.sh T                 # 시나리오 지정
#
# 변조 실행: 유형의 시나리오(§4.1 표에서 "—"가 아닌 칸)를 한 게임 안에서 차례로 넣는다.
#   - 실행 시간을 시나리오 수로 나눈 구간마다 시나리오 하나, 구간 안에 COUNT건(기본 20 = §4.2 시드당 20건)
#   - 간격 = max(500, 구간/(COUNT+1) − 지터/2) ms + 0~200ms 지터 (§4.2 "간격 ≥ 500ms")
#   - 시나리오마다 labels_<시나리오>.jsonl·injector_<시나리오>.json, 합친 labels.jsonl
# 채점:
#   - score.json: 실행 전체(라벨 전부). 오탐(FP)·오탐률은 이 파일이 기준
#   - score_<시나리오>.json: 그 시나리오 라벨만. TP·FN·stale·지연은 이 파일을 쓰고, 여기의 FP는 다른 시나리오 변조의
#     alert까지 FP로 세므로 쓰지 않는다
# 요약 표: python3 -I scripts/summarize_experiment.py results/<날짜>_<실험>_*
#
# 결과 폴더: results/<날짜>_<실험>_<시드>/ (실험 = normal | tamper-T | tamper-V | tamper-H).
# 원본(addr·alerts·labels_*)은 고치지 않는다. 같은 날 다시 돌리면 폴더 이름 뒤에 -2, -3...을 붙인다.
set -euo pipefail
cd "$(dirname "$0")/.."

TYPE=${1:-}
case "$TYPE" in
  normal) EXP=normal; DEFAULT_SCENARIOS="" ;;
  T) EXP=tamper-T; DEFAULT_SCENARIOS="T-timer T-mines_remaining T-cell_state T-cells_opened" ;;
  V) EXP=tamper-V; DEFAULT_SCENARIOS="V-timer V-mines_remaining V-mine_field V-cell_state V-cells_opened V-game_state" ;;
  H) EXP=tamper-H; DEFAULT_SCENARIOS="H-timer H-mines_remaining H-cell_state H-cells_opened" ;;
  *) echo "usage: $0 normal|T|V|H   (env: SEEDS DURATION SPEED_MS DIFFICULTY COUNT SCENARIOS)" >&2; exit 2 ;;
esac

SEEDS=${SEEDS:-"1 2 3 4 5"}
DURATION=${DURATION:-600}
SPEED_MS=${SPEED_MS:-50}
DIFFICULTY=${DIFFICULTY:-hard}
COUNT=${COUNT:-20}
JITTER_MS=200
SCENARIOS=${SCENARIOS:-$DEFAULT_SCENARIOS}
RESULTS=${RESULTS:-results}
DATE=$(date +%Y-%m-%d)

if [[ "$TYPE" != normal ]]; then
  for s in $SCENARIOS; do
    [[ "$s" == "$TYPE"-* ]] || { echo "scenario $s is not type $TYPE (§4.2: one type per run)" >&2; exit 2; }
  done
fi

COMMIT=$(git rev-parse HEAD)
DIRTY=false
if ! git diff --quiet HEAD -- libmtd game injector scripts CMakeLists.txt; then DIRTY=true; fi

# 호스트 기록 (§2)
HOST_OS="$(uname -srm)"
if [[ "$(uname -s)" == "Darwin" ]]; then
  HOST_CPU="$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)"
  HOST_CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo unknown)"
  HOST_VIRT="Docker Desktop (macOS)"
else
  HOST_CPU="$(lscpu 2>/dev/null | sed -n 's/^Model name:[[:space:]]*//p' | head -1)"
  HOST_CORES="$(nproc 2>/dev/null || echo unknown)"
  HOST_VIRT="native Docker"
fi
DOCKER_VERSION="$(docker version --format '{{.Server.Version}}' 2>/dev/null || echo unknown)"

# 시드별 폴더 (덮어쓰지 않음)
DIRS=()
for seed in $SEEDS; do
  dir="$RESULTS/${DATE}_${EXP}_${seed}"
  n=2
  while [[ -e "$dir" ]]; do dir="$RESULTS/${DATE}_${EXP}_${seed}-${n}"; n=$((n + 1)); done
  mkdir -p "$dir"
  DIRS+=("$dir")
done

CONCURRENT=$(echo $SEEDS | wc -w | tr -d ' ')
echo "exp=$EXP seeds=[$SEEDS] duration=${DURATION}s speed_ms=$SPEED_MS scenarios=[$SCENARIOS] count=$COUNT concurrent=$CONCURRENT commit=${COMMIT:0:7} dirty=$DIRTY"

export EXP SEEDS DURATION SPEED_MS DIFFICULTY COUNT JITTER_MS SCENARIOS COMMIT DIRTY CONCURRENT \
       HOST_OS HOST_CPU HOST_CORES HOST_VIRT DOCKER_VERSION
export DIRS_STR="${DIRS[*]}"

INNER='
set -euo pipefail
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j >/dev/null
read -ra seeds <<< "$SEEDS"; read -ra dirs <<< "$DIRS_STR"; read -ra scen <<< "$SCENARIOS"
GXX="$(g++ --version | head -1)"; CNPROC="$(nproc)"

run_one() {  # $1 seed, $2 dir
  local s=$1 d=$2
  local gcmd="./build/game/minesweeper --bot --difficulty $DIFFICULTY --seed $s --speed-ms $SPEED_MS --duration-sec $DURATION --addr-log $d/addr.jsonl --alert-log $d/alerts.jsonl --stats $d/stats.json"
  echo "$gcmd" > "$d/command.txt"
  $gcmd > "$d/stdout.txt" 2> "$d/stderr.txt" &
  local gpid=$!
  local icodes=()
  if (( ${#scen[@]} > 0 )); then
    local slot=$(( DURATION / ${#scen[@]} ))
    local interval=$(( slot * 1000 / (COUNT + 1) - JITTER_MS / 2 ))
    (( interval < 500 )) && interval=500
    # 주소 로그에 첫 state_alloc이 나올 때까지 기다린다
    for _ in $(seq 1 100); do grep -q state_alloc "$d/addr.jsonl" 2>/dev/null && break; sleep 0.05; done
    for sc in "${scen[@]}"; do
      local icmd="./build/injector/mtd_injector --pid $gpid --addr-log $d/addr.jsonl --labels $d/labels_$sc.jsonl --scenario $sc --count $COUNT --interval-ms $interval --jitter-ms $JITTER_MS --duration-sec $slot --seed $s --stats $d/injector_$sc.json"
      echo "$icmd" >> "$d/command.txt"
      if $icmd >> "$d/injector_stdout.txt" 2>> "$d/injector_stderr.txt"; then icodes+=("\"$sc\":0"); else icodes+=("\"$sc\":$?"); fi
      [[ -f "$d/labels_$sc.jsonl" ]] || : > "$d/labels_$sc.jsonl"
    done
  fi
  local gcode=0
  wait "$gpid" || gcode=$?
  # 라벨 합치기 (시나리오 순서 = 시간 순서)
  : > "$d/labels.jsonl"
  for sc in "${scen[@]}"; do cat "$d/labels_$sc.jsonl" >> "$d/labels.jsonl"; done
  local inj_codes
  inj_codes=$(IFS=,; echo "${icodes[*]:-}")
  SEED=$s GCODE=$gcode INJ_CODES="{$inj_codes}" START="$start" END="$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  GXX="$GXX" CNPROC="$CNPROC" python3 -I - "$d/command.txt" "$d/meta.json" <<"PY"
import json, os, sys
e = os.environ
cmds = open(sys.argv[1]).read().splitlines()
meta = {"experiment": e["EXP"], "seed": int(e["SEED"]), "difficulty": e["DIFFICULTY"],
        "speed_ms": int(e["SPEED_MS"]), "duration_sec": int(e["DURATION"]),
        "scenarios": e["SCENARIOS"].split(), "count_per_scenario": int(e["COUNT"]),
        "command": cmds[0], "injector_commands": cmds[1:],
        "exit_code": int(e["GCODE"]), "injector_exit_codes": json.loads(e["INJ_CODES"]),
        "commit": e["COMMIT"], "dirty": e["DIRTY"] == "true", "build": "Release",
        "rules": "game/rules.json", "period_ms": 50, "enable_mprotect": 0, "window_ms": 200,
        "concurrent_runs": int(e["CONCURRENT"]), "start_utc": e["START"], "end_utc": e["END"],
        "container": {"image": "mh-safer-dev", "compiler": e["GXX"], "nproc": int(e["CNPROC"])},
        "host": {"os": e["HOST_OS"], "cpu": e["HOST_CPU"], "cores": e["HOST_CORES"],
                 "virtualization": e["HOST_VIRT"], "docker": e["DOCKER_VERSION"]}}
with open(sys.argv[2], "w") as f:
    json.dump(meta, f, ensure_ascii=False, indent=1)
PY
  rm -f "$d/command.txt"
  local sargs="--alerts $d/alerts.jsonl --addr-log $d/addr.jsonl --stats $d/stats.json --meta $d/meta.json"
  python3 -I scripts/score.py --labels "$d/labels.jsonl" $sargs > "$d/score_stdout.txt"
  for sc in "${scen[@]}"; do
    python3 -I scripts/score.py --labels "$d/labels_$sc.jsonl" $sargs --output "$d/score_$sc.json" >> "$d/score_stdout.txt"
  done
  echo "$d game_exit=$gcode injector={$inj_codes} labels=$(wc -l < "$d/labels.jsonl" | tr -d " ")"
}

start=$(date -u +%Y-%m-%dT%H:%M:%SZ)
pids=()
for i in "${!seeds[@]}"; do run_one "${seeds[$i]}" "${dirs[$i]}" & pids+=($!); done
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
exit $fail
'

scripts/dev.sh env EXP="$EXP" SEEDS="$SEEDS" DURATION="$DURATION" SPEED_MS="$SPEED_MS" DIFFICULTY="$DIFFICULTY" \
  COUNT="$COUNT" JITTER_MS="$JITTER_MS" SCENARIOS="$SCENARIOS" COMMIT="$COMMIT" DIRTY="$DIRTY" \
  CONCURRENT="$CONCURRENT" HOST_OS="$HOST_OS" HOST_CPU="$HOST_CPU" HOST_CORES="$HOST_CORES" \
  HOST_VIRT="$HOST_VIRT" DOCKER_VERSION="$DOCKER_VERSION" DIRS_STR="${DIRS[*]}" bash -lc "$INNER"

python3 -I scripts/summarize_experiment.py "${DIRS[@]}"

#!/usr/bin/env bash
# S3-05 정상 플레이 로그 수집 (docs/experiment.md §3, §6).
# 개발 컨테이너에서 Release 빌드 후 시드별 봇 실행을 동시에 돌려 results/<날짜>_normal_<시드>/에 남긴다.
#
#   scripts/collect-normal.sh                 # 시드 1~5, 600초, --speed-ms 50
#   SEEDS="1 2" DURATION=60 scripts/collect-normal.sh
#
# 결과 폴더: meta.json(조건) · addr.jsonl · alerts.jsonl · stats.json · stdout.txt · stderr.txt
# 원본(addr·alerts)은 고치지 않는다. 같은 날 다시 돌리면 폴더 이름 뒤에 -2, -3...을 붙인다.
set -euo pipefail
cd "$(dirname "$0")/.."

SEEDS=${SEEDS:-"1 2 3 4 5"}
DURATION=${DURATION:-600}
SPEED_MS=${SPEED_MS:-50}
DIFFICULTY=${DIFFICULTY:-hard}
DATE=$(date +%Y-%m-%d)

COMMIT=$(git rev-parse HEAD)
DIRTY=false
if ! git diff --quiet HEAD -- libmtd game CMakeLists.txt; then DIRTY=true; fi

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
  dir="results/${DATE}_normal_${seed}"
  n=2
  while [[ -e "$dir" ]]; do dir="results/${DATE}_normal_${seed}-${n}"; n=$((n + 1)); done
  mkdir -p "$dir"
  DIRS+=("$dir")
done

CONCURRENT=$(echo $SEEDS | wc -w | tr -d ' ')
echo "seeds=[$SEEDS] duration=${DURATION}s speed_ms=$SPEED_MS concurrent=$CONCURRENT commit=${COMMIT:0:7} dirty=$DIRTY"

# 컨테이너 안: 빌드 → 동시 실행 → meta.json
scripts/dev.sh env \
  SEEDS="$SEEDS" DIRS="${DIRS[*]}" DURATION="$DURATION" SPEED_MS="$SPEED_MS" DIFFICULTY="$DIFFICULTY" \
  COMMIT="$COMMIT" DIRTY="$DIRTY" CONCURRENT="$CONCURRENT" HOST_OS="$HOST_OS" HOST_CPU="$HOST_CPU" \
  HOST_CORES="$HOST_CORES" HOST_VIRT="$HOST_VIRT" DOCKER_VERSION="$DOCKER_VERSION" \
  bash -lc '
set -euo pipefail
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j >/dev/null
read -ra seeds <<< "$SEEDS"; read -ra dirs <<< "$DIRS"
GXX="$(g++ --version | head -1)"; CNPROC="$(nproc)"
start=$(date -u +%Y-%m-%dT%H:%M:%SZ)
pids=()
for i in "${!seeds[@]}"; do
  s=${seeds[$i]}; d=${dirs[$i]}
  cmd="./build/game/minesweeper --bot --difficulty $DIFFICULTY --seed $s --speed-ms $SPEED_MS --duration-sec $DURATION --addr-log $d/addr.jsonl --alert-log $d/alerts.jsonl --stats $d/stats.json"
  echo "$cmd" > "$d/command.txt"
  $cmd > "$d/stdout.txt" 2> "$d/stderr.txt" &
  pids+=($!)
done
codes=()
for p in "${pids[@]}"; do if wait "$p"; then codes+=(0); else codes+=($?); fi; done
end=$(date -u +%Y-%m-%dT%H:%M:%SZ)
for i in "${!seeds[@]}"; do
  s=${seeds[$i]}; d=${dirs[$i]}
  cat > "$d/meta.json" <<JSON
{"experiment":"normal","seed":$s,"difficulty":"$DIFFICULTY","speed_ms":$SPEED_MS,"duration_sec":$DURATION,
 "command":"$(cat "$d/command.txt")","exit_code":${codes[$i]},
 "commit":"$COMMIT","dirty":$DIRTY,"build":"Release","rules":"game/rules.json","period_ms":50,"enable_mprotect":0,
 "concurrent_runs":$CONCURRENT,"start_utc":"$start","end_utc":"$end",
 "container":{"image":"mh-safer-dev","compiler":"$GXX","nproc":$CNPROC},
 "host":{"os":"$HOST_OS","cpu":"$HOST_CPU","cores":"$HOST_CORES","virtualization":"$HOST_VIRT","docker":"$DOCKER_VERSION"}}
JSON
  rm -f "$d/command.txt"
  echo "$d exit=${codes[$i]} $(tail -1 "$d/stdout.txt")"
done
'

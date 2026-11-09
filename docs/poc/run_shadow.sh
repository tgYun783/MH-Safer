#!/usr/bin/env bash
# S1-12 섀도 복사 + 검사 PoC
set -u
cd "$(dirname "$0")"
COUNT=100
INTERVAL_MS=250   # 주입 간격(> 탐지주기 100ms, 라운드 겹침 방지)

echo "== 컴파일 =="
gcc -O2 -Wall -Wextra -pthread -o shadow_target   shadow_target.c   || exit 1
gcc -O2 -Wall -Wextra          -o shadow_injector shadow_injector.c || exit 1
echo ok; echo

rm -f target.info detect.log inject.log
./shadow_target &
TPID=$!
for _ in $(seq 1 50); do [ -s target.info ] && break; sleep 0.05; done
read -r pid addr < target.info
echo "target pid=$pid  secret@=$addr  (검사주기 100ms, 주입 ${COUNT}회 @${INTERVAL_MS}ms)"
echo

echo "== 외부 변조 ${COUNT}회 (/proc/pid/mem) =="
./shadow_injector "$pid" "${addr#0x}" "$COUNT" "$INTERVAL_MS"
sleep 0.5                 # 마지막 라운드 탐지 여유
kill "$TPID" 2>/dev/null; wait "$TPID" 2>/dev/null
echo

echo "== 집계 =="
awk -v C="$COUNT" '
  FNR==NR {
    if ($1=="INJECT") { t0[$3]=$4 }   # val -> inject_ns
    next
  }
  $1=="DETECT" {
    v=$2; td=$3
    if (v in t0 && !(v in seen)) {
      seen[v]=1
      lat=(td - t0[v])/1000000.0      # ms
      det++
      if (lat<=200) within++
      sum+=lat; if (lat>max||max==0) max=lat; if (min==0||lat<min) min=lat
    }
  }
  $1=="FALSE" { falsecnt++ }
  END {
    printf "탐지: %d / %d\n", det+0, C
    printf "200ms 이내: %d / %d\n", within+0, det+0
    if (det>0) printf "지연(ms)  min=%.1f  avg=%.1f  max=%.1f\n", min, sum/det, max
    printf "정상 timer 오탐(FALSE): %d 건  (기대 0)\n", falsecnt+0
    pass = (det+0==C) && (within+0==det+0) && (falsecnt+0==0)
    printf "판정: %s\n", pass ? "PASS" : "FAIL"
  }
' inject.log detect.log

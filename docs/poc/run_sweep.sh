#!/usr/bin/env bash
# 검사 주기(T) 스윕: T마다 ① 탐지율·지연 ② 검사 스레드 CPU 측정.
set -u
cd "$(dirname "$0")"

PERIODS="25 50 75 100"
COUNT=80          # 탐지 측정 주입 횟수
INTERVAL_MS=150   # 주입 간격 (> T_max, 라운드 겹침 방지)
CPU_FILLERS=1000  # CPU 측정용 감시 변수 수
CPU_SECONDS=4     # CPU 측정 지속 시간

echo "== 컴파일 =="
gcc -O2 -Wall -Wextra -pthread -o sweep_target    sweep_target.c    || exit 1
gcc -O2 -Wall -Wextra          -o shadow_injector shadow_injector.c || exit 1
echo ok; echo

ROWS=""
for T in $PERIODS; do
    echo "===== T = ${T}ms ====="

    # ---------- ① 탐지율·지연 (fillers=0) ----------
    rm -f target.info detect.log inject.log
    ./sweep_target "$T" 0 &
    TPID=$!
    for _ in $(seq 1 50); do [ -s target.info ] && break; sleep 0.05; done
    read -r pid addr < target.info
    ./shadow_injector "$pid" "${addr#0x}" "$COUNT" "$INTERVAL_MS" 2>/dev/null
    sleep 0.4
    kill -TERM "$TPID" 2>/dev/null; wait "$TPID" 2>/dev/null

    read -r det within lat_avg lat_max falsecnt <<EOF
$(awk -v C="$COUNT" '
  FNR==NR { if ($1=="INJECT") t0[$3]=$4; next }
  $1=="DETECT" { v=$2; td=$3; if (v in t0 && !(v in seen)){ seen[v]=1;
      lat=(td-t0[v])/1e6; det++; if(lat<=200)within++; sum+=lat; if(lat>max)max=lat } }
  $1=="FALSE" { fc++ }
  END { printf "%d %d %.1f %.1f %d", det+0, within+0, (det?sum/det:0), max+0, fc+0 }
' inject.log detect.log)
EOF

    # ---------- ② 검사 스레드 CPU (fillers=N, 주입 없음) ----------
    rm -f target.info cpu.txt
    ./sweep_target "$T" "$CPU_FILLERS" &
    TPID=$!
    for _ in $(seq 1 50); do [ -s target.info ] && break; sleep 0.05; done
    sleep "$CPU_SECONDS"
    kill -TERM "$TPID" 2>/dev/null; wait "$TPID" 2>/dev/null

    cpu_ns=$(sed -n 's/.*cpu_ns=\([0-9]*\).*/\1/p' cpu.txt)
    polls=$(sed -n 's/.*polls=\([0-9]*\).*/\1/p' cpu.txt)
    cpu_pct=$(awk -v c="$cpu_ns" -v s="$CPU_SECONDS" 'BEGIN{printf "%.2f", c/1e9/s*100}')
    cpu_ms=$(awk -v c="$cpu_ns" 'BEGIN{printf "%.1f", c/1e6}')

    printf "  탐지 %d/%d, 200ms이내 %d, 지연 avg %.1f / max %.1f ms, 오탐 %d\n" \
           "$det" "$COUNT" "$within" "$lat_avg" "$lat_max" "$falsecnt"
    printf "  CPU(%d변수, %ds): %s ms = %s%%, polls=%s\n\n" \
           "$CPU_FILLERS" "$CPU_SECONDS" "$cpu_ms" "$cpu_pct" "$polls"

    ROWS+="| ${T} | ${det}/${COUNT} | ${within} | ${lat_avg} | ${lat_max} | ${cpu_pct} |"$'\n'
done

echo "===== 요약 (markdown) ====="
echo "검사변수 ${CPU_FILLERS}개, CPU측정 ${CPU_SECONDS}s 기준"
echo "| T(ms) | 탐지율 | ≤200ms | 지연 avg(ms) | 지연 max(ms) | 검사 CPU(%) |"
echo "|---|---|---|---|---|---|"
printf "%s" "$ROWS"

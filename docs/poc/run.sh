#!/usr/bin/env bash
# S1-10 / S1-11 PoC 실행기. gcc:13 컨테이너 안에서 돈다.
set -u
cd "$(dirname "$0")"

echo "== 아키텍처: $(uname -m) / 커널 $(uname -r) =="
echo "== yama ptrace_scope: $(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo '(없음)') =="
echo

echo "== 컴파일 =="
gcc -O2 -Wall -Wextra -o selftest selftest.c guard.c || exit 1
gcc -O2 -Wall -Wextra -o target   target.c   guard.c || exit 1
gcc -O2 -Wall -Wextra -o injector injector.c          || exit 1
echo "ok"; echo

echo "=================== S1-10: 프로세스 내부 쓰기 ==================="
./selftest
echo

echo "=================== S1-11: 외부 쓰기 경로별 ==================="
STATUS_OUT=""
run_one () {
    local method="$1"
    rm -f target.info target.status
    ./target &
    local tpid=$!
    # target.info 생길 때까지 대기
    for _ in $(seq 1 50); do [ -s target.info ] && break; sleep 0.05; done
    read -r pid addr < target.info

    sleep 0.2
    local before; before=$(cat target.status)
    local val_before; val_before=$(echo "$before" | grep -o 'val=0x[0-9a-f]*')
    local f_before;   f_before=$(echo "$before"   | grep -o 'faults=[0-9]*')

    # 주입
    local inj; inj=$(./injector "$pid" "${addr#0x}" "$method" 2>&1); local irc=$?

    sleep 0.3
    local after; after=$(cat target.status)
    local val_after; val_after=$(echo "$after" | grep -o 'val=0x[0-9a-f]*')
    local f_after;   f_after=$(echo "$after"   | grep -o 'faults=[0-9]*')

    kill "$tpid" 2>/dev/null; wait "$tpid" 2>/dev/null

    local wrote="아니오"; [ "$val_before" != "$val_after" ] && wrote="예"
    local faulted="아니오"; [ "$f_before" != "$f_after" ] && faulted="예"
    printf "  %-8s | 주입기: %s\n" "$method" "$inj"
    printf "           | %s → %s\n" "$val_before" "$val_after"
    printf "           | 쓰기성공: %-4s  대상 SIGSEGV: %-4s  (%s / %s)\n" \
           "$wrote" "$faulted" "$f_before" "$f_after"
    echo
    STATUS_OUT+="| \`$method\` | $wrote | $faulted |"$'\n'
}

run_one vm
run_one proc
run_one ptrace

echo "=================== 표 요약 (markdown) ==================="
echo "| 경로 | 쓰기 성공 | 대상 SIGSEGV |"
echo "|---|---|---|"
printf "%s" "$STATUS_OUT"

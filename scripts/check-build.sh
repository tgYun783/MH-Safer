#!/usr/bin/env bash
# 빌드만 확인한다 (증분). Claude Code 훅·git pre-commit 훅이 같이 쓴다 (S2-03).
#   - Linux에 cmake가 있으면 로컬로, 없으면 scripts/dev.sh(Docker)로 빌드
#   - 성공: 한 줄 출력, 종료코드 0 / 실패: 오류 요약을 stderr로, 종료코드 1
#   - Docker를 못 쓰면 종료코드 3 (빌드를 확인하지 못함)
#   테스트까지 돌리려면 scripts/dev.sh build.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=$(mktemp -t mtd-build.XXXXXX)
trap 'rm -f "$LOG"' EXIT

BUILD_CMD='cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j'

if [[ "$(uname -s)" == "Linux" ]] && command -v cmake >/dev/null 2>&1; then
  bash -c "$BUILD_CMD" >"$LOG" 2>&1
  rc=$?
else
  if ! docker info >/dev/null 2>&1; then
    echo "check-build: Docker가 실행 중이 아니라 빌드를 확인하지 못했습니다 (Docker Desktop을 켜세요)." >&2
    exit 3
  fi
  scripts/dev.sh bash -lc "$BUILD_CMD" >"$LOG" 2>&1
  rc=$?
fi

if [[ $rc -eq 0 ]]; then
  echo "check-build: 빌드 성공"
  exit 0
fi

{
  echo "check-build: 빌드 실패 (종료코드 $rc). 오류 요약:"
  # 컴파일 오류 줄을 먼저, 없으면 로그 끝부분
  if grep -nE 'error:|Error [0-9]+|undefined reference' "$LOG" | head -20 | grep -q .; then
    grep -E 'error:|undefined reference' "$LOG" | head -20
    echo "---- 로그 끝 ----"
  fi
  tail -15 "$LOG"
} 2>&1 | sed "s#/work/##g" >&2
exit 1

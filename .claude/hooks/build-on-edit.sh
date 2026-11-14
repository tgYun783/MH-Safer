#!/usr/bin/env bash
# Claude Code PostToolUse 훅 (S2-03): C/C++ 파일을 고치면 빌드를 돌리고,
# 실패하면 종료코드 2 + stderr로 오류를 대화에 보여 준다.
set -uo pipefail
input=$(cat)
file=$(printf '%s' "$input" | jq -r '.tool_input.file_path // empty')

case "$file" in
  *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*CMakeLists.txt) ;;
  *) exit 0 ;;
esac
# 저장소 밖 파일·PoC(별도 빌드)는 건너뜀
case "$file" in
  "$CLAUDE_PROJECT_DIR"/docs/*|"$CLAUDE_PROJECT_DIR"/build*) exit 0 ;;
  "$CLAUDE_PROJECT_DIR"/*) ;;
  *) exit 0 ;;
esac

out=$("$CLAUDE_PROJECT_DIR/scripts/check-build.sh" 2>&1)
rc=$?
case $rc in
  0) exit 0 ;;
  3) # Docker 없음: 막지 않고 사용자에게만 알림
     jq -n --arg m "$out" '{systemMessage: $m}'
     exit 0 ;;
  *) printf '%s\n' "$out" >&2
     exit 2 ;;
esac

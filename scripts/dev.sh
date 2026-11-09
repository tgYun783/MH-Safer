#!/usr/bin/env bash
# 개발 컨테이너를 빌드하고 들어간다 (또는 인자로 준 명령 실행).
# ptrace/proc-mem PoC를 위해 SYS_PTRACE capability를 부여한다.
#
#   scripts/dev.sh                 # 셸 진입
#   scripts/dev.sh build           # 빌드+테스트 한 번
#   scripts/dev.sh <명령...>       # 컨테이너 안에서 명령 실행
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=mh-safer-dev
docker build -t "$IMAGE" -f docker/Dockerfile .

RUN=(docker run --rm -i
     --cap-add=SYS_PTRACE --security-opt seccomp=unconfined
     -v "$PWD":/work -w /work "$IMAGE")

case "${1:-}" in
  build)
    "${RUN[@]}" bash -lc \
      'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
       cmake --build build -j && \
       ctest --test-dir build --output-on-failure'
    ;;
  "")
    exec "${RUN[@]/-i/-it}" bash   # 대화형 셸
    ;;
  *)
    "${RUN[@]}" "$@"
    ;;
esac

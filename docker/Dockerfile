# MH-Safer 개발 환경 (팀 공통). Ubuntu 24.04 = g++ 13.
# ptrace/proc-mem 실험은 컨테이너 실행 시 --cap-add=SYS_PTRACE 로 켠다 (scripts/dev.sh).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ \
        gcc \
        cmake \
        make \
        gdb \
        clang-format \
        python3 \
        python3-pip \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["bash"]

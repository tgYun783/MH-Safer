# AGENTS.md — MH-Safer 개발 지침

Claude Code·Codex 등 코딩 에이전트가 이 저장소에서 작업할 때 따르는 공통 지침이다.
사람도 이 문서를 개발 가이드로 쓴다.

## 프로젝트 목적
행위 기반 탐지와 예측을 통한 **메모리 변조 탐지 라이브러리(libmtd)**. Linux에서 실행 중인
프로그램(첫 사용처: 터미널 지뢰찾기)의 핵심 변수에 대한 외부 프로세스 변조를 실시간 탐지한다.
자세한 목표·범위는 `docs/plan.md`, 주차별 작업은 `docs/sprints/`.

## 폴더 구조
```
libmtd/        감시 라이브러리 (주력: 섀도 복사+검사, 보조: mprotect)
  include/     공개 API: mtd.h (C, extern "C"), mtd.hpp (C++ RAII 래퍼)
  src/         구현
  tests/       doctest 단위 테스트
game/          T1 지뢰찾기 + 자동 플레이 봇
injector/      T2 변조 주입기 (시나리오 3종, 정답 라벨)
bench/         T5 자원 측정
examples/      T4 이식성 검증용 두 번째 앱
third_party/   헤더 전용 단일 파일 벤더링 (doctest 등)
docs/          계획·스프린트·설계·인터페이스 규격·PoC 결과
  poc/         감시 방식 PoC 코드와 결과 (write-paths.md)
```

## 모듈별 지침 목록
작업 전 해당 폴더의 `AGENTS.md`를 읽는다 (각 폴더 `CLAUDE.md`는 `@AGENTS.md`만 담는다).

| 폴더 | 지침 | 내용 |
|---|---|---|
| `libmtd/` | [libmtd/AGENTS.md](libmtd/AGENTS.md) | 감시 엔진·탐지 계층 담당, API 경계·감시 수명·콜백 주의점 |
| `game/` | [game/AGENTS.md](game/AGENTS.md) | 감시 변수 6종, 구간 안 변경 규칙, 수명 순서(주소 로그) |
| `injector/` | [injector/AGENTS.md](injector/AGENTS.md) | 주소 로그 기반 대상 선정, 정답 라벨, 대상 제한 |
| `bench/` | [bench/AGENTS.md](bench/AGENTS.md) | 측정 조건 기록, "미측정" 원칙 |

## 빌드·테스트 명령
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```
개발 환경은 Linux. macOS/Windows에서는 Docker로 통일한다:
```bash
docker run --rm -v "$PWD":/src -w /src gcc:13 bash -lc \
  'apt-get -qq update && apt-get -qq install -y cmake >/dev/null; \
   cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure'
```
PoC(쓰기 경로 실험)는 ptrace/`/proc/mem` 권한이 필요하다:
```bash
docker run --rm --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
  -v "$PWD/docs/poc":/poc -w /poc gcc:13 bash run.sh        # S1-10/11
# bash run_shadow.sh 로 S1-12
```

## 코딩 규칙
- **언어**: 라이브러리·게임·주입기·평가 도구는 C++17, 분석·그래프는 Python. 경고는 `-Wall -Wextra -Werror`.
- **공개 API 경계** (`libmtd/include/mtd.h`): `extern "C"` 함수로만 내보낸다. 경계 밖으로 **C++ 예외·STL 타입을 넘기지 않는다** (컴파일러·표준 라이브러리 버전이 달라도 링크되게).
- **의존성**: C++ 표준 라이브러리·pthread만. 외부 라이브러리는 **헤더 전용 단일 파일**만 `third_party/`에 벤더링(JSON=nlohmann/json, 테스트=doctest). 서드파티 헤더는 `SYSTEM` include로 경고 제외.
- **시그널 핸들러**(mprotect 백엔드): async-signal-safe 코드만. 메모리 할당·예외·락·`std::` 컨테이너 조작 금지. 미리 할당한 고정 버퍼에 기록.
- **프로그램별 지식**(변수 규칙)은 코드가 아니라 **규칙 파일**(`rules.json`)에 둔다. 라이브러리에 게임 지식을 넣지 않는다.
- 커밋·PR은 한 작업 단위로. `main` 직접 푸시 금지(아래 완료 기준).

## 금지 사항
- 커널 권한 공격, 탐지기 자체 무력화, 실제 상용 프로그램 적용 (범위 밖 — `docs/plan.md`).
- 공개 API 경계로 C++ 예외/STL 타입 전달.
- 서드파티를 소스 트리에 통째로 복사(단일 헤더만).
- `main`에 직접 커밋.

## 완료 기준 (Definition of Done)
`docs/sprints/README.md`와 동일:
- 완료 기준(인수 조건)을 모두 만족하고, 확인 방법을 실제로 돌려 봤다
- PR로 `main`에 병합, 다른 팀원 1명 리뷰
- CI 빌드·테스트 통과
- 바뀐 내용이 있으면 해당 폴더 문서(`AGENTS.md`·`docs/`) 갱신

## 참고 문서
- `docs/plan.md` — 목표·기능·프로세스·간트
- `docs/sprints/` — 스프린트 운영·백로그
- `docs/monitoring-design.md` — 감시 엔진 설계 (mtd.h v1 근거)
- `docs/interface.md` — 이벤트/구간·판정·정답 라벨 형식
- `docs/poc/write-paths.md` — 감시 방식 PoC 결과

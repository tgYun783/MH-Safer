# 배열 비교 오버헤드 (S2-14)

**상태** 측정 코드 작성 · **결과 미측정**(측정 후 아래 표를 채운다)
**질문** `mine_field`·`cell_state`(각 480바이트)를 바이트 비교하면(11-11 결정, `docs/monitoring-design.md` §8) 검사 1회 비용과 CPU는 얼마인가
**코드** `bench/array_compare.cpp` (실제 libmtd 엔진 사용) · 재현:
```bash
scripts/dev.sh bash -lc 'cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ./build/bench/bench_array_compare 5'
```

## 방법
- **측정 1 (마이크로)**: 변수 1개 검사 = `memcpy`(실제값 스냅샷) + `memcmp`(섀도 비교). 엔진의 `detect_outside`와 같은 연산. 값이 같아 `memcmp`가 끝까지 읽는 정상 상태(최악 비용) 기준, 200만 회 평균.
- **측정 2 (엔진)**: libmtd 검사 스레드를 T = 50ms로 돌리고 메인 스레드는 잠든 채, 프로세스 CPU 시간(`CLOCK_PROCESS_CPUTIME_ID`)을 잰다. 이 값은 검사 스레드 CPU와 거의 같다. 검사 1회 비용 = CPU 시간 / (경과 시간 / T).
  - 스칼라만: 4개(`timer`·`mines_remaining`·`cells_opened`·`game_state`)
  - GameState 1개: 스칼라 4 + 배열 2 (게임의 실제 구성)
  - GameState 100개: 600변수(배열 200), 확장 여유 확인용

## 결과
**측정일** 미측정 · **환경** 미측정

### 측정 1: 변수 1개 검사 비용
| 크기(바이트) | ns/회 |
|---|---|
| 4 | 미측정 |
| 480 | 미측정 |

### 측정 2: 검사 스레드 CPU (T = 50ms)
| 구성 | 감시 변수 | 배열(480B) | 검사 CPU(%) | 검사 1회(us) |
|---|---|---|---|---|
| 스칼라만 | 4 | 0 | 미측정 | 미측정 |
| GameState 1개 | 6 | 2 | 미측정 | 미측정 |
| GameState 100개 | 600 | 200 | 미측정 | 미측정 |

## 해석
측정 후 작성.

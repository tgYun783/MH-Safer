# game (T1)

시드 기반 지뢰찾기와 화면 없는 안전 칸 자동 플레이 봇.

## 빌드·실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/game/minesweeper
./build/game/minesweeper --difficulty hard
./build/game/minesweeper --bot --seed 42 --speed-ms 5 --duration-sec 600
./build/game/minesweeper --bot --seed 42 --speed-ms 0 --duration-sec 10 --addr-log /tmp/addr.jsonl
./build/game/minesweeper --bot --difficulty hard --seed 1 --speed-ms 50 --duration-sec 600 \
  --addr-log addr.jsonl --alert-log alerts.jsonl --stats stats.json   # 실험 조건(docs/experiment.md §3)
./build/game/minesweeper --no-mtd        # libmtd 없이 실행
```

## libmtd 감시 (S3-02)
기본으로 libmtd가 켜져 있고 `--rules`(기본: 소스 트리의 `game/rules.json`)로 초기화합니다.

| 옵션 | 뜻 |
|---|---|
| `--rules FILE` | 규칙 파일. 로드에 실패하면 종료 코드 2 |
| `--no-mtd` | libmtd를 켜지 않음 |
| `--alert-log FILE` | alert를 JSONL로 기록(`docs/interface.md` §2). 배열 변수의 `expected`/`actual`은 `null` |
| `--stats FILE` | 실행 요약 JSON: 판 수, `alerts`, `mtd_end_count`(오탐률 분모, `docs/experiment.md` §7), `watch_errors` |

봇 상태 줄과 대화형 화면 머리에 `alerts=N`이 나옵니다.

연결 구조: 게임 코어(`minesweeper_core`)는 libmtd에 의존하지 않고 `StateObserver` 훅만 냅니다.
`mtd_glue.cpp`(`MtdMonitor`)가 훅을 libmtd 호출로 바꿉니다.

| 훅 | libmtd |
|---|---|
| `state_alloc` | `mtd_watch` 6개(`mine_field` 제외, `safe_cells` 포함) → 주소 로그 `state_alloc` |
| `field_watch` | `mtd_watch(mine_field)` → 주소 로그 `field_watch` |
| `state_free` | 주소 로그 `state_free` → `mtd_unwatch` (메모리 해제 전) |
| `scope_begin`/`scope_end` | `mtd_begin`/`mtd_end`: `tick`(timer), `flag`(mines_remaining), `reveal`(cell_state·cells_opened), `win-check`(game_state) |
| `event` | `mtd_event("input")`: `open()` 진입 시, `reveal` 구간 직전 |

새 판은 이전 판을 먼저 해제한 뒤 만듭니다. 그래서 감시 수는 배치 전 6개, 배치 후 7개를 넘지 않습니다.

대화형 조작은 `w/a/s/d`로 이동하고 `o`로 열기, `f`로 깃발을 놓거나
회수합니다. 승리·패배 후 `n`으로 새 판을 시작할 수 있습니다. 자동 플레이는
지뢰 배치를 알고 안전한 칸만 무작위로 열며, 종료 시 새 판을 시작합니다.
`--duration-sec 0`(기본값)은 중단할 때까지 실행합니다.

감시 대상 상태는 `GameState`에 모여 있습니다. 보드는 최대 30×16이며,
`StateAllocator` 콜백을 통해 상태 구조체 할당기를 교체할 수 있습니다.
`--addr-log <파일>`은 판 상태 생성·지뢰 배치 직후·해제 직전에 JSONL 주소 로그를
기록합니다. `StateObserver` 콜백은 세 수명 지점에 외부 감시 등록을 연결합니다.

## 후속 작업 (PR #10 리뷰에서 나온 것)

- **상태 수명 지점(완료)**: 판마다 `GameState`를 새로 할당하는 구조는 유지한다.
  수명 지점(생성 직후·지뢰 배치 직후·해제 직전)마다 같은 주소 정보를 libmtd와 주입기 양쪽에 넘긴다.
  - 생성 직후: `mine_field`를 뺀 6개(`safe_cells` 포함) `mtd_watch` → `--addr-log`에 `state_alloc` 기록
  - 지뢰 배치 직후(첫 클릭): `mine_field` `mtd_watch` → `field_watch` 기록
  - 해제 직전: `state_free` 기록 → 감시 중인 변수 `mtd_unwatch` → 메모리 해제
  - 로그 형식·순서는 `docs/interface.md` §3.1, API는 `docs/monitoring-design.md` §3·§8.
- **승패 판정 구간 분리(완료)**: 칸 열기와 승패 대입을 별도 함수로 분리했다.
  libmtd 연결 시 칸 열기는 `reveal`, 승패 대입(lost/won)은 `win-check` 구간으로 감싼다
  (`docs/rules-format.md`).
- **정상 동작 데이터 수집 시 봇 속도**: `--speed-ms 0`이면 한 판이 1초 안에 끝나
  `timer`가 한 번도 오르지 않는다(10분 실행 시 `timer=0s`). 오탐률 측정용 실행은
  한 판이 수 초 이상 이어지도록 `--speed-ms`를 준다.
- **정리(완료)**: 패배 시 지뢰를 표시하고, 종료 후 처리 중복을 제거했다.

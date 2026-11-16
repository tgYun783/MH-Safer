# 인터페이스 규격 (S1-04, v0)

**상태** 2026-11-09 작성 · **v1 확정** 2026-11-11 (`mtd.h` v1과 함께) · S3 채점·일관성 결정은 §6~§7
**짝 문서** `libmtd/include/mtd.h` (공개 API), `docs/monitoring-design.md` (감시 엔진 설계)

모듈 4개(감시 라이브러리 L·게임 T1·주입기 T2·평가 T3)가 공유하는 데이터 형식을 정의한다.
여기서 정하는 건 **① 이벤트/구간**, **② 판정(alert) 로그**, **③ 정답 라벨**(+ 주소 로그 §3.1), 그리고 셋을 잇는 **채점 규칙**이다.
규칙 파일(`rules.json`) 내부 구조는 S1-05에서 별도로 정한다(여기선 참조만).

## 공통 규약
- **시각**: 모든 타임스탬프는 `CLOCK_MONOTONIC` 나노초(`uint64_t`). 같은 머신의 프로세스 간 비교 가능(PoC에서 검증). 벽시계(wall clock) 쓰지 않는다.
- **값 표기**: 스칼라(≤8바이트)는 `0x` 16진수 문자열. 배열/구조체 등 비스칼라는 값 대신 `null`.
- **로그 형식**: 한 줄에 JSON 하나(JSONL). 파싱은 nlohmann/json(헤더 전용) 사용.

## 1. 이벤트와 구간

두 가지를 구분한다.

| 종류 | API | 뜻 | 예 |
|---|---|---|---|
| **구간(tag)** | `mtd_begin(tag)` / `mtd_end()` | 이 사이의 변화는 **합법**. `mtd_end`에서 규칙 검사 후 섀도 동기화 | `"tick"`, `"flag"`, `"reveal"`, `"win-check"` |
| **사건(event)** | `mtd_event(name)` | 순간 신호. "이 사건 직후 N ms 안에서만 특정 변수가 바뀔 수 있다"는 행위 규칙의 근거 | `"input"` |

**지뢰찾기 초기 등록(안)** — 변수↔구간/사건 매핑은 `rules.json`(S1-05)에 둔다:

| 변수 | 합법 구간/사건 |
|---|---|
| 타이머 | 구간 `"tick"` |
| 남은 지뢰 | 구간 `"flag"` |
| 칸 상태 / 열린 칸 수 | 사건 `"input"` 직후 구간 `"reveal"` |
| 게임 상태 | 구간 `"win-check"` (승리·패배 모두. 칸 열기 `"reveal"`과 분리 — 11-11 결정) |
| 승리 검사 기준(`safe_cells`) | 판 생성 때 계산 후 불변. 게임별 규칙의 일관성 검사에서 참조 |
| 지뢰 배치 | 없음(첫 클릭 후 불변) → 어떤 변화든 행위 위반. 감시는 지뢰 배치 직후부터(§3.1 `field_watch`) |

## 2. 판정(alert) 로그 형식

`mtd_alert`(mtd.h) 1건 = JSONL 1줄.

```json
{"detect_ns":120345678,"name":"mine_field","kind":"behavior","rule":"immutable","expected":null,"actual":null}
```

| 필드 | 뜻 |
|---|---|
| `detect_ns` | 탐지 시각 (CLOCK_MONOTONIC) |
| `name` | 감시 변수 이름 |
| `kind` | `"behavior"`(타이밍) \| `"prediction"`(값) |
| `rule` | 위반한 규칙/태그 id, 없으면 `null` |
| `expected` / `actual` | 섀도/실제값. 비스칼라면 `null` |

## 3. 정답 라벨 형식 (주입기 T2 출력)

주입기가 변조 1건마다 JSONL 1줄을 남긴다. 평가 도구가 "실제로 무엇을 언제 변조했는지"의 **정답**으로 쓴다.

```json
{"inject_ns":120300000,"name":"mine_field","addr":"0x55d0c3a1e2a8","method":"proc","scenario":"V-mine_field","from":null,"to":null,"hidden":false}
```

| 필드 | 뜻 |
|---|---|
| `inject_ns` | 변조 시각 (CLOCK_MONOTONIC) |
| `name` | 변조한 변수 이름 (감시 이름과 동일 키) |
| `addr` | 실제로 쓴 주소(16진수 문자열). 주소 로그(§3.1)의 어느 수명 구간을 노렸는지 대조용 (11-11 추가) |
| `method` | `"internal"` \| `"vm"` \| `"proc"` \| `"ptrace"` (S1-11 경로) |
| `scenario` | 시나리오 식별자 (주입기 3종 라벨) |
| `from` / `to` | 변조 전/후 값, 비스칼라면 `null` |
| `hidden` | 은닉형(정상 패턴 흉내) 여부. `true`면 탐지율 목표에서 제외하고 한계로 집계 |

## 3.1 주소 로그 형식 (게임 T1 출력, 11-11 추가)

감시 메모리는 재할당될 수 있다(지뢰찾기는 판마다 `GameState`를 새로 할당). 게임은 테스트 옵션 `--addr-log <파일>`을 주면 수명 지점마다 JSONL 1줄을 남기고, 주입기는 이 로그로 대상 주소를 정한다. 메모리 스캔은 하지 않는다.

```json
{"ns":120000000,"event":"state_alloc","addr":"0x55d0c3a1e2a0","size":1476,"fields":[{"name":"timer","offset":0,"size":4},{"name":"mines_remaining","offset":4,"size":4},{"name":"cell_state","offset":488,"size":480},{"name":"cells_opened","offset":968,"size":4},{"name":"safe_cells","offset":972,"size":4},{"name":"game_state","offset":976,"size":4}]}
{"ns":120100000,"event":"field_watch","addr":"0x55d0c3a1e2a0","fields":[{"name":"mine_field","offset":8,"size":480}]}
{"ns":125000000,"event":"state_free","addr":"0x55d0c3a1e2a0"}
```

| 필드 | 뜻 |
|---|---|
| `ns` | 기록 시각 (CLOCK_MONOTONIC) |
| `event` | `"state_alloc"` \| `"field_watch"` \| `"state_free"` |
| `addr` / `size` | 상태 구조체 시작 주소·전체 크기 |
| `fields[]` | 이 시점에 감시를 시작한 변수별 `name`(규칙 파일·감시 이름과 같은 키)·`offset`·`size`. `state_alloc`·`field_watch`에만 |

**순서 보장** (게임이 지킨다)
- 생성: `mine_field`를 뺀 감시 변수마다 `mtd_watch` 완료 → `state_alloc` 기록. 주입기가 주소를 알 때는 이미 감시 중이다.
- 지뢰 배치(첫 클릭): 배치 완료 → `mine_field` `mtd_watch` → `field_watch` 기록. 배치 전에는 `mine_field`가 감시 대상이 아니다(11-11 결정).
- 해제: `state_free` 기록 → 감시 중인 변수마다 `mtd_unwatch` → 메모리 해제.

**주입기 규칙**: 가장 최근 `state_alloc`의 `addr`에, `state_alloc`·`field_watch`로 알려진 변수의 `offset`을 더해 쓴다. 아직 알려지지 않은 변수(배치 전 `mine_field`)에는 쓰지 않는다. 그 `addr`의 `state_free`를 본 뒤에는 그 주소에 쓰지 않는다.

## 4. 채점 규칙 (평가 도구 T3)

정답 라벨(§3)과 판정 로그(§2)를 `name`으로 묶고 시각으로 대조한다. **탐지 윈도우 W = 200ms**(검사 주기의 최악 2배, S1-12 근거).

- **참 양성(TP)**: 라벨 1건에 대해, 같은 `name`의 alert가 `inject_ns ≤ detect_ns ≤ inject_ns + W` 안에 **하나 이상** 존재
- **거짓 음성(FN)**: 라벨에 매칭 alert 없음
- **거짓 양성(FP)**: 어느 유효(비-stale) 라벨의 탐지 윈도우와도 매칭되지 않은 alert (정상 변경을 때린 것)
- **지표**:
  - 탐지율 = TP / (`hidden=false`이고 stale이 아닌 라벨 수)  — 목표 ≥ 90%
  - 오탐률 = FP / 합법 구간 수(`mtd_end` 호출 수)  — 목표 ≤ 1%. 10분당 FP 건수도 함께 보고 (11-16 PM 결정, `docs/experiment.md` §7)
  - 탐지 지연 = `detect_ns - inject_ns` 분포 — 목표 ≤ 100ms
- 은닉형(`hidden=true`)은 탐지율에서 빼고 **별도 한계 지표**로 보고.
- **감시 밖 변조 제외 (11-11 추가)**: 라벨의 `addr`가 `[state_alloc.addr, state_alloc.addr + size)` 안에 드는 수명 구간(주소 로그 §3.1의 `state_alloc`~`state_free`)을 찾는다. 그 구간이 끝난 뒤의 변조, 즉 `inject_ns ≥ state_free.ns`이면 감시 대상이 없던 쓰기이므로 FN에서 빼고 "stale"로 따로 센다. 해당 변수의 감시 시작(`state_alloc` 또는 `field_watch`) 전에 들어간 변조도 같은 방식으로 뺀다.

## 5. v0에서 바뀐 점 (plan.md §2 초안 대비)
PoC(섀도 주력) 반영:
- `mtd_init(path, on_alert)` → `mtd_init(path, cfg)` : 콜백·검사주기·백엔드 토글을 `mtd_config`로 모음
- `mtd_alloc` 필수 → **선택**(섀도만 쓰면 불필요)
- `mtd_alert`에 **`kind`(behavior/prediction)** 추가 → 채점에서 행위/예측 분리 집계
- 이벤트 외에 **구간(tag)** 을 1급 개념으로 명시

11-11 `mtd.h` v1 확정 반영 (`docs/monitoring-design.md` §8):
- 정답 라벨에 `addr` 추가, 주소 로그(§3.1) 신설, 채점에 감시 밖(해제 후·감시 시작 전) 변조 제외 규칙 추가
- 지뢰 배치는 배치 직후부터 감시(`field_watch`)
- 게임 상태의 `win-check` 구간이 패배 전환도 포함

## 6. 결정 (S3-21, 2026-11-16)
- [x] 탐지 윈도우 W는 검사 주기와 분리해 **200ms로 고정**한다. 실행에서 `--window-ms`로 바꾼 경우 score 결과에 사용값을 기록한다.
- [x] 비스칼라 변수의 `expected`/`actual`은 공통 규약대로 `null`이다. 채점기는 이 필드를 해석하지 않고 `name`·`kind`·시각만 사용한다.
- [x] 한 라벨은 같은 `name`의 alert가 W 안에 하나 이상이면 TP **1건**이다. 매칭 alert가 여러 개여도 TP를 중복 계산하지 않고, 각각 behavior/prediction 집계에는 반영한다. 매칭된 alert는 FP가 아니다.
- [x] stale은 주소 로그에서 해당 주소·변수의 감시 수명이 주입 시각을 포함하지 않는 라벨이다. FN 및 탐지율 분모에서 제외하고 별도로 센다. 은닉형은 별도 탐지 지표로 보고한다.

## 7. 변수 간 일관성 alert (S3-23)
- 일관성 규칙 위반은 `kind="prediction"`, `rule`은 consistency 규칙의 `name`으로 기록한다.
- alert `name`은 `require.variable`, `expected`는 `require.equals.variable`의 실제값, `actual`은 `require.variable`의 실제값이다. 예: `cells_opened`가 `safe_cells`와 다르면 두 수치를 함께 기록한다.
- 일관성은 `when.variable`이 바뀐 `mtd_end`에서 검사한다. 필요한 변수가 감시되지 않았거나 스칼라가 아니면 규칙을 건너뛰고 규칙별 stderr 경고를 한 번 낸다.
- 한 라벨의 지연은 매칭 alert 중 가장 이른 alert로 계산한다. AND 결합에서는 쌍의 두 alert가 모두 라벨의 W 안에 있어야 검출로 보고, 지연은 더 늦은 alert 시각을 기준으로 한다.

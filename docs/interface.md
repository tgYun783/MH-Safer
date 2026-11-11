# 인터페이스 규격 (S1-04, v0)

**상태** v0 초안 · 2026-11-09 · **확정** 11-11 (`mtd.h` v1과 함께)
**짝 문서** `libmtd/include/mtd.h` (공개 API), `docs/monitoring-design.md` (감시 엔진 설계)

모듈 3개(감시 라이브러리 L·게임 T1·주입기 T2·평가 T3)가 공유하는 데이터 형식을 정의한다.
여기서 정하는 건 **① 이벤트/구간**, **② 판정(alert) 로그**, **③ 정답 라벨**, 그리고 셋을 잇는 **채점 규칙**이다.
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
| 지뢰 배치 | 없음(첫 클릭 후 불변) → 어떤 변화든 행위 위반 |

## 2. 판정(alert) 로그 형식

`mtd_alert`(mtd.h) 1건 = JSONL 1줄.

```json
{"detect_ns":120345678,"name":"mine_field","kind":"behavior","rule":"immutable","expected":"0x2222222222222222","actual":"0xdeadbeefcafebabe"}
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
{"inject_ns":120300000,"name":"mine_field","addr":"0x55d0c3a1e2a8","method":"proc","scenario":"S1-immutable","from":"0x2222222222222222","to":"0xdeadbeefcafebabe","hidden":false}
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
{"ns":120000000,"event":"state_alloc","addr":"0x55d0c3a1e2a0","size":1472,"fields":[{"name":"timer","offset":0,"size":4},{"name":"mines_remaining","offset":4,"size":4},{"name":"mine_field","offset":8,"size":480}]}
{"ns":125000000,"event":"state_free","addr":"0x55d0c3a1e2a0"}
```

| 필드 | 뜻 |
|---|---|
| `ns` | 기록 시각 (CLOCK_MONOTONIC) |
| `event` | `"state_alloc"` \| `"state_free"` |
| `addr` / `size` | 상태 구조체 시작 주소·전체 크기 |
| `fields[]` | 감시 변수별 `name`(규칙 파일·감시 이름과 같은 키)·`offset`·`size`. `state_alloc`에만 |

**순서 보장** (게임이 지킨다)
- 생성: 감시 변수마다 `mtd_watch` 완료 → `state_alloc` 기록. 주입기가 주소를 알 때는 이미 감시 중이다.
- 해제: `state_free` 기록 → 감시 변수마다 `mtd_unwatch` → 메모리 해제.

**주입기 규칙**: 가장 최근 `state_alloc`의 `addr + offset`에 쓴다. 그 `addr`의 `state_free`를 본 뒤에는 그 주소에 쓰지 않는다.

## 4. 채점 규칙 (평가 도구 T3)

정답 라벨(§3)과 판정 로그(§2)를 `name`으로 묶고 시각으로 대조한다. **탐지 윈도우 W = 200ms**(검사 주기의 최악 2배, S1-12 근거).

- **참 양성(TP)**: 라벨 1건에 대해, 같은 `name`의 alert가 `inject_ns ≤ detect_ns ≤ inject_ns + W` 안에 **하나 이상** 존재
- **거짓 음성(FN)**: 라벨에 매칭 alert 없음
- **거짓 양성(FP)**: 어떤 라벨과도 매칭 안 되는 alert (정상 변경을 때린 것)
- **지표**:
  - 탐지율 = TP / (`hidden=false`인 라벨 수)  — 목표 ≥ 90%
  - 오탐률 = FP / 전체 판정 수  — 목표 ≤ 1%
  - 탐지 지연 = `detect_ns - inject_ns` 분포 — 목표 ≤ 100ms
- 은닉형(`hidden=true`)은 탐지율에서 빼고 **별도 한계 지표**로 보고.
- **해제된 주소 변조 제외 (11-11 추가)**: 라벨의 `addr`가 속한 수명 구간(주소 로그 §3.1의 `state_alloc`~`state_free`) 밖에서 들어간 변조, 즉 `inject_ns ≥ state_free.ns`이면 감시 대상이 없던 쓰기이므로 FN에서 빼고 "stale"로 따로 센다.

## 5. v0에서 바뀐 점 (plan.md §2 초안 대비)
PoC(섀도 주력) 반영:
- `mtd_init(path, on_alert)` → `mtd_init(path, cfg)` : 콜백·검사주기·백엔드 토글을 `mtd_config`로 모음
- `mtd_alloc` 필수 → **선택**(섀도만 쓰면 불필요)
- `mtd_alert`에 **`kind`(behavior/prediction)** 추가 → 채점에서 행위/예측 분리 집계
- 이벤트 외에 **구간(tag)** 을 1급 개념으로 명시

11-11 PM 결정 반영 (`docs/monitoring-design.md` §8, 우진·전유정 확인 대기):
- 정답 라벨에 `addr` 추가, 주소 로그(§3.1) 신설, 채점에 해제된 주소 변조 제외 규칙 추가
- 게임 상태의 `win-check` 구간이 패배 전환도 포함

## 6. 열린 질문 (11-11 확정)
- [ ] 탐지 윈도우 W(200ms)를 검사 주기와 연동할지 고정할지
- [ ] 비스칼라 변수(지뢰 배치·칸 상태)의 `expected/actual` 표현 (해시 문자열?) — S1-05·T3와 연동
- [ ] 한 라벨에 alert가 여럿일 때(behavior+prediction 동시) 채점 집계 방식

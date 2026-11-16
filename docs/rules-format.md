# 규칙 파일 형식 (rules.json, S1-05, v0)

**상태** 스키마 v0 · 2026-11-09 작성 · **확정** 2026-11-11 (`mtd.h` v1 / `interface.md` v1과 함께) · 남은 열린 질문은 담당 스프린트에서 결정
**짝 문서** `docs/interface.md`(형식 규격), `docs/monitoring-design.md`(탐지 엔진), `libmtd/include/mtd.h`
**예시 파일** `game/rules.json` (지뢰찾기)

이 파일은 "프로그램별 지식"(어떤 변수가 언제·어떻게 바뀌나)을 전부 담는다.
libmtd 코어에는 게임 지식을 넣지 않는다(plan.md §2). 파싱은 nlohmann/json.

## 최상위 필드
| 필드 | 뜻 | 근거 |
|---|---|---|
| schema_version | 규칙 스키마 버전("0") | mtd.h/interface와 버전 동기 |
| program | 대상 프로그램 식별자 | 한 라이브러리가 여러 규칙 파일 대응 |
| detect_window_ms | 채점 탐지 윈도우 W(기본 200) | interface.md §4 W=200ms |
| default_event_window_ms | 사건→변경 허용 간격 기본(50) | mtd_config 기본 주기 50ms(mtd.h)와 연동 |
| variables[] | 감시 변수 규칙 목록 | - |

## variables[] 항목
| 필드 | 뜻 |
|---|---|
| name | 감시 변수 이름. mtd_watch/alert/정답 라벨의 공통 키(interface.md §2/§3) |
| value | 자료형·크기·비교 방식 힌트 |
| behavior | 행위 기반 규칙: 변화가 합법인 타이밍 |
| prediction | 예측 기반 규칙: 합법 구간 안 변화가 만족할 값 규칙 |

### value (자료형/크기 힌트)
- type: "scalar" | "array"
- size(스칼라, 바이트) / element_size·count(배열, count=null이면 런타임 결정)
- signed, encoding("int"|"uint"|"enum")
- compare: "value"(스칼라 직접 비교) | "bytes"(비스칼라 바이트 비교, 11-11 결정. 해시는 alert 표현용으로만)
- scalar=false인 비스칼라는 alert의 expected/actual이 null(interface.md §공통규약·§2)
- enum.codes: 상태 코드값. 지뢰찾기는 `[0,1,2]`(11-11 승인)

### behavior (행위 기반)
| 필드 | 뜻 |
|---|---|
| mutable | 변할 수 있는가. false면 어떤 변화든 위반 |
| allowed_in_tags | 변화가 합법인 구간 tag 목록. 이 밖 변화 = 행위 위반(mtd_begin/mtd_end) |
| required_event | 구간이 유효하려면 선행해야 할 사건 이름, 없으면 null |
| event_window_ms | 사건 후 변경 허용 시간창, 없으면 default 사용 |

검사 엔진 대응(S2-13 결정, 아래 "S2-13 결정" 절): required_event는 **구간 게이트**다.
검사 스레드는 구간 밖 실제≠섀도를 사건 유무와 관계없이 behavior alert로 낸다.
구간 안 변화는 `mtd_end`에서, 그 구간 시작(`mtd_begin`) 전 event_window_ms 안에 required_event가 없었으면 behavior alert(`rule="required_event"`).

### prediction (예측 기반) — rule + params
| rule | params | 의미 |
|---|---|---|
| delta_eq | delta | 변화량이 정확히 N |
| delta_in | deltas[] | 변화량이 집합 안 |
| monotonic | direction | 스칼라 단조(non_decreasing 등) |
| immutable | - | 값 불변(변화 자체가 위반) |
| per_element_monotonic | direction | 배열 요소별 단방향 |
| enum_transition | allowed[][] | 허용된 상태 전이쌍만 |

검사 시점: 합법 구간 mtd_end에서 rule_ok로 검사(monitoring-design §4).

## 변수별 규칙과 근거 (6종)
- timer: tag "tick" 안 +1만. 1초마다 +1(plan.md §1) → delta_eq 1. 구간 밖 변화/+1 아님 탐지.
- mines_remaining: tag "flag" 안 ±1만. 깃발 꽂기 -1·빼기 +1(plan.md §1) → delta_in [-1,1].
- mine_field: 첫 클릭 후 불변(interface.md §1 "구간 없음", plan.md §1) → mutable=false + immutable. 배열이라 compare=bytes. 감시는 지뢰 배치 직후 시작(11-11 결정, interface.md §3.1 `field_watch`)이라 배치 자체는 위반이 아니다.
- cell_state: 사건 "input" 직후 tag "reveal" 안에서만, 열린 칸 안 닫힘(plan.md §1) → required_event=input + per_element_monotonic(open_only). 배열.
- cells_opened: input 직후 reveal 안, 단조 증가(plan.md §1) → monotonic non_decreasing. cell_state와 짝.
- game_state: tag "win-check" 안에서만 상태 변경(interface.md §1, 승리·패배 모두. 게임은 칸 열기와 승패 대입을 다른 구간으로 분리 — 11-11 결정) → enum_transition. 승리 정당성은 변수 간 일관성이라 별도(아래 열린 질문).

## 설계 선택(근거 1줄씩)
- behavior/prediction을 한 변수에 분리 수납: 탐지 2지점(mtd_end=예측, 검사 스레드=행위)에 1:1 대응(monitoring-design §4/mtd.h kind).
- 배열(mine_field·cell_state)은 scalar=false + compare=bytes: 요소별 규칙(per_element_monotonic)을 검사하려면 바이트 비교가 필요(11-11 결정). alert 값은 interface.md §공통규약대로 null, mtd.h expected/actual=0과 일관.
- immutable을 behavior(mutable=false)와 prediction(immutable) 양쪽에 둠: mine_field는 "구간 없음+값 불변"이 모두 참이라 두 탐지 경로에서 다 위반으로 잡히게 함.
- required_event를 "구간 유효 조건"으로 해석(input 없는 reveal 자체가 행위 위반): interface.md §1 "input 직후 reveal" 문구 반영.

## 열린 질문 (전유정 S1-24와 조율, 체크된 것은 11-11 결정)
- [x] 배열 비교 방식 → **바이트 비교**(11-11 결정). per_element_monotonic을 요소별로 검사할 수 있어 cell_state 규칙 실현 가능. (monitoring-design §3/§8)
- [ ] 비스칼라 변수의 expected/actual 표현(해시 문자열 등) — alert 로그·채점(T3)과 연동. (interface.md §6) → **S3 최소 채점기(전유정)**
- [ ] game_state 승리 정당성("모든 안전 칸 오픈")은 변수 간 일관성 검사(L4). 규칙 파일이 cross-check(cells_opened 등 참조)를 어떻게 표현할지 스키마 확장 필요 — 현재 enum_transition는 전이쌍만 검사. (plan.md §1, 기능 L4) → **S2-24 스키마 초안(선택), 구현은 S3**
- [x] required_event의 정확한 의미론 → **구간 게이트만**(S2-13). 비동기 수용 조건으로 쓰지 않는다. 아래 "S2-13 결정" 절.
- [x] event_window_ms를 검사 주기 T와 연동할지 → **고정**(규칙 파일 값, 주기와 무관). 아래 "S2-13 결정" 절.
- [x] 자료형·크기·enum 코드값 → PR #10 구현값으로 **승인**(11-11): timer `uint32`, mines_remaining `int32`, game_state 코드 `[0,1,2]`(playing/won/lost), 배열은 `uint8`(지뢰=1, 열림=1) 최대 30×16칸. 배열 count는 난이도별 런타임 결정이라 null 유지.
- [x] game_state의 `lost` 전환 태그 → **게임 코드에서 판정 분리**(11-11 결정). 규칙은 `win-check`만 허용 그대로 두고, 게임이 칸 열기는 `reveal` 구간, 승패 대입(lost/won)은 `win-check` 구간으로 감싼다. 합법 구간이 좁아져 reveal 중 승리 위조를 행위 탐지로 잡을 수 있다. 현재 구현(PR #10)은 `open()` 안에서 `lost`를 대입하므로 libmtd 연동 때 수정 필요(전유정).
- [x] 변수 이름 키 표준 → **현재 이름으로 확정**(11-11): `timer`, `mines_remaining`, `mine_field`, `cell_state`, `cells_opened`, `game_state` (snake_case English). `mtd_watch` 이름·alert `name`·정답 라벨 `name`·주소 로그 `fields[].name`의 공통 키.

## S2-13 결정 (2026-11-13, 우진 제안 · 전유정 승인은 PR 리뷰로)
내부 인터페이스는 `libmtd/src/rules.hpp`(규칙 조회 `RuleSet::find`)와 `libmtd/src/prediction.hpp`(`mtd_end`의 예측 검사 `check_prediction`). 파서·예측 검사는 stub(`rules_stub.cpp`·`prediction_stub.cpp`)으로 시작하고 S2-21·S2-22가 교체한다.

**required_event = 구간 게이트**
- 구간 밖 변화: 사건이 있었든 없었든 behavior alert(`rule="allowed_in_tags"`, `mutable=false`면 `"immutable"`).
- 구간 안 변화: `mtd_end`에서 마지막 required_event 시각 `e`와 그 구간의 `mtd_begin` 시각 `b`를 비교해 `e + event_window_ms ≥ b`가 아니면(사건이 없었거나 너무 오래됨) behavior alert(`rule="required_event"`). 구간 안에서 사건이 난 경우(`e > b`)도 허용.
- 근거: monitoring-design §4 의사코드처럼 "구간 밖이라도 사건 직후면 수용"하면 `input` 직후 50ms 동안 외부 변조가 모두 합법이 된다(주입기가 사건에 맞춰 쓰면 은닉). 게임은 칸 열기를 항상 `reveal` 구간으로 감싸므로(interface.md §1) 비동기 수용이 필요 없다. 위 "설계 선택"의 "input 없는 reveal 자체가 행위 위반"과 같은 해석.

**event_window_ms = 고정(주기 T와 무관)**
- 사건 시각과 구간 시작 시각은 둘 다 게임 스레드에서 동기로 찍힌다. 검사 스레드 주기가 끼지 않으므로 T와 연동할 이유가 없다.
- 값은 변수별 `event_window_ms`, null이면 `default_event_window_ms`(50). 파서가 채워서 `BehaviorSpec::event_window_ms`에 넣는다(엔진은 0을 받지 않는다고 가정).

**규칙 없는 변수**: 규칙 파일이 없거나 변수 이름이 규칙에 없으면 **어느 구간이든 합법**(tag 무관), 구간 밖 변화만 behavior alert(`rule=null`), 예측 검사 없음.

**mtd_end 예측 검사 계약**: 구간 안에서 값이 바뀐 변수만, 엔진 락을 잡은 채로 `check_prediction(rule, before=섀도, after=실제값, size)`를 1회 호출. 배열은 같은 포인터로 요소별 비교(배열 섀도 접근). `ok=false`면 prediction alert 1건(`rule=rule_id`). 반환 후 섀도는 실제값으로 동기화(ack). 같은 변화가 required_event 위반이면 behavior와 prediction alert가 둘 다 날 수 있다.

## 미정 요약
- 배열 count: 난이도별 런타임 결정(null). 나머지 자료형·크기·enum 코드는 11-11 승인.
- cell_state per_element_monotonic 실제 검사 알고리즘: 바이트 비교로 결정됨, 구현은 S2(미구현).

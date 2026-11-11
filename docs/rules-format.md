# 규칙 파일 형식 (rules.json, S1-05, v0)

**상태** v0 초안 · 2026-11-09 · **확정** 11-11 (mtd.h v1 / interface.md v0와 함께)
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
- compare: "value"(스칼라 직접 비교) | "hash"(비스칼라 블록 해시)
- scalar=false인 비스칼라는 alert의 expected/actual이 null(interface.md §공통규약·§2)
- enum.codes=null은 상태 코드값 미정(게임 미구현)

### behavior (행위 기반)
| 필드 | 뜻 |
|---|---|
| mutable | 변할 수 있는가. false면 어떤 변화든 위반 |
| allowed_in_tags | 변화가 합법인 구간 tag 목록. 이 밖 변화 = 행위 위반(mtd_begin/mtd_end) |
| required_event | 구간이 유효하려면 선행해야 할 사건 이름, 없으면 null |
| event_window_ms | 사건 후 변경 허용 시간창, 없으면 default 사용 |

검사 엔진 대응(monitoring-design §4): 검사 스레드가 구간 밖에서 실제≠섀도를 보면,
required_event가 event_window_ms 안에 있었으면 수용, 아니면 behavior alert.

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
- mine_field: 첫 클릭 후 불변(interface.md §1 "구간 없음", plan.md §1) → mutable=false + immutable. 배열이라 compare=hash.
- cell_state: 사건 "input" 직후 tag "reveal" 안에서만, 열린 칸 안 닫힘(plan.md §1) → required_event=input + per_element_monotonic(open_only). 배열.
- cells_opened: input 직후 reveal 안, 단조 증가(plan.md §1) → monotonic non_decreasing. cell_state와 짝.
- game_state: tag "win-check" 안에서만 상태 변경(interface.md §1) → enum_transition. 승리 정당성은 변수 간 일관성이라 별도(아래 열린 질문).

## 설계 선택(근거 1줄씩)
- behavior/prediction을 한 변수에 분리 수납: 탐지 2지점(mtd_end=예측, 검사 스레드=행위)에 1:1 대응(monitoring-design §4/mtd.h kind).
- 배열(mine_field·cell_state)은 scalar=false + compare=hash: interface.md §공통규약(비스칼라 값은 null), mtd.h expected/actual=0과 일관.
- immutable을 behavior(mutable=false)와 prediction(immutable) 양쪽에 둠: mine_field는 "구간 없음+값 불변"이 모두 참이라 두 탐지 경로에서 다 위반으로 잡히게 함.
- required_event를 "구간 유효 조건"으로 해석(input 없는 reveal 자체가 행위 위반): interface.md §1 "input 직후 reveal" 문구 반영.

## 11-11 확정 전 열린 질문 (전유정 S1-24와 조율)
- [ ] 배열 비교 방식(바이트 vs 블록 해시). per_element_monotonic은 요소별 비교가 필요해 단일 해시로는 "단방향성"을 못 봄 — cell_state 규칙 실현 가능성이 여기 달림. (monitoring-design §3/§8, interface.md §6)
- [ ] 비스칼라 변수의 expected/actual 표현(해시 문자열 등) — alert 로그·채점(T3)과 연동. (interface.md §6)
- [ ] game_state 승리 정당성("모든 안전 칸 오픈")은 변수 간 일관성 검사(L4). 규칙 파일이 cross-check(cells_opened 등 참조)를 어떻게 표현할지 스키마 확장 필요 — 현재 enum_transition는 전이쌍만 검사. (plan.md §1, 기능 L4)
- [ ] required_event의 정확한 의미론: 구간(tag) 게이트인가, 비동기 검사 경로의 수용 조건인가, 둘 다인가 — monitoring-design §4 의사코드(needs_event/recent_event)와 맞물림.
- [ ] event_window_ms를 검사 주기 T(mtd_config.period_ms)와 연동할지 고정할지 — interface.md §6 W 연동 질문과 같은 축.
- [ ] 자료형·크기·enum 코드값은 전부 힌트일 뿐 미정: T1 게임 구조체 확정 후 size/signed/enum.codes/count 채워야 함.
  - PR #10에서 구현값을 `game/rules.json`에 반영함(timer `uint32`, game_state 코드 `[0,1,2]`, 배열은 `uint8` 최대 30×16칸). 11-11에 확정할 것.
- [ ] game_state의 `lost` 전환 태그: 구현(PR #10)에서는 지뢰를 연 `open()` 안, 즉 `reveal` 구간에서 `lost`로 바뀌는데 규칙은 `win-check` 태그만 허용 → libmtd 연동 시 오탐. 규칙에 `reveal` 허용을 추가할지, 게임 코드에서 승패 판정을 `win-check` 구간으로 분리할지.
- [ ] 변수 이름 키 표준(snake_case English) 합의 — interface.md 예시(mine_field)를 따랐으나 6개 전체 네이밍은 미합의.

## 미정 요약
- 6개 변수의 C 자료형·바이트 크기·game_state enum 코드·배열 count: 게임 미구현으로 전부 미정(힌트값만 기재).
- cell_state per_element_monotonic 실제 검사 알고리즘: 배열 비교 방식 결정에 종속(미정).

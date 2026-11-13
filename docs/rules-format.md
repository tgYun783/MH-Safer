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
- [ ] game_state 승리 정당성("모든 안전 칸 오픈")은 변수 간 일관성 검사(L4). 규칙 파일이 cross-check(cells_opened 등 참조)를 어떻게 표현할지 스키마 확장 필요 — 현재 enum_transition는 전이쌍만 검사. (plan.md §1, 기능 L4) → **S2-24 초안 제출(선택), 회의 채택 여부 결정, 구현은 S3**
- [x] `required_event` 의미론 → 동기 경로에서는 허용 tag와 함께 게이트로 사용하며, 사건이 `mtd_begin` 시점 기준 `event_window_ms` 안에 있어야 한다. 비동기 검사 경로는 사건 후 지정 시간 안의 변화를 수용한다. 허용 tag 자체를 대신하지 않는다. → **S2-13(우진·전유정)**
- [x] `event_window_ms` → 고정 밀리초 시간창이며 검사 주기 `T`와 독립적이다. 변수별 값이 null이면 `default_event_window_ms`를 쓴다. → **S2-13(우진·전유정)**
- [x] 자료형·크기·enum 코드값 → PR #10 구현값으로 **승인**(11-11): timer `uint32`, mines_remaining `int32`, game_state 코드 `[0,1,2]`(playing/won/lost), 배열은 `uint8`(지뢰=1, 열림=1) 최대 30×16칸. 배열 count는 난이도별 런타임 결정이라 null 유지.
- [x] game_state의 `lost` 전환 태그 → **게임 코드에서 판정 분리**(11-11 결정). 칸 열기와 승패 대입을 별도 함수로 분리했으며, libmtd 연동 시 칸 열기는 `reveal`, 승패 대입은 `win-check` 구간으로 감싼다.
- [x] 변수 이름 키 표준 → **현재 이름으로 확정**(11-11): `timer`, `mines_remaining`, `mine_field`, `cell_state`, `cells_opened`, `game_state` (snake_case English). `mtd_watch` 이름·alert `name`·정답 라벨 `name`·주소 로그 `fields[].name`의 공통 키.

## 미정 요약
- 배열 count: 난이도별 런타임 결정(null). 나머지 자료형·크기·enum 코드는 11-11 승인.
- cell_state per_element_monotonic 실제 검사 알고리즘: 바이트 비교를 바탕으로 S2에서 구현.

### 변수 간 일관성 표현 초안 (S2-24, 미채택)

승리 상태는 보드 크기와 지뢰 수를 함께 알아야 검증할 수 있으므로, 변수별
`prediction`과 분리된 최상위 `consistency` 절을 제안한다. 아래는 형식 초안이며
S3 구현 전 팀 회의에서 채택 여부를 결정한다.

```json
{
  "consistency": [
    {
      "name": "won_requires_all_safe_cells_open",
      "when": { "variable": "game_state", "equals": "won" },
      "require": {
        "variable": "cells_opened",
        "equals": { "expression": "rows * columns - mine_count" }
      }
    }
  ]
}
```

`when`이 참일 때 `require`를 검사한다. `expression`은 규칙 파일에서 임의 코드를
실행하는 용도가 아니며, 이 제안에서는 보드 메타데이터 산술식만 허용한다. 표현식
문법·변수 참조·alert 형식과 실행 시점은 채택 후 S3에서 구체화한다.

# 감시 엔진 설계 (mtd.h v1 근거) — 초안

**상태** 초안 · 2026-11-09 작성 · 2026-11-11 PM 결정 반영(§8, 우진·전유정 확인 대기) · **확정 예정** 2026-11-11 (`mtd.h` v1)
**근거** `docs/poc/write-paths.md` (S1-10/11/12 실측)
**관련** `docs/plan.md` §2, `docs/sprints/sprint-1.md`

> 이 문서는 S1 PoC 결과를 바탕으로 감시 엔진의 구조와 공개 API를 확정하기 위한 초안이다.
> `[결정 필요]` 표시는 11-11 회의에서 정한다.

## 1. 결정 요약

PoC로 확인된 사실 (`docs/poc/write-paths.md`):
- `mprotect`+SIGSEGV는 **프로세스 내부 쓰기만** 잡는다. 외부 변조 경로 `/proc/<pid>/mem`·`ptrace`는 보호를 무시하고 써지며 대상에 SIGSEGV가 나지 않는다.
- **섀도 복사+주기 검사**는 변조 경로와 무관하게 값 변화만으로 외부 변조 100/100을 200ms 내 탐지, 정상 변경 오탐 0.

**결정**
1. **감시 주력 = 섀도 복사 + 주기 검사.** 모든 감시 변수는 섀도 사본을 가지며, 검사 스레드가 주기적으로 실제값과 비교한다.
2. **`mprotect`+SIGSEGV = 보조 백엔드.** 프로세스 내부 쓰기(코드 주입 등) 탐지용. 기본 비활성, 옵션으로 켠다.
3. **플랫폼 의존 코드는 보조(mprotect)에만.** 주력(섀도)은 표준 C++/pthread만 쓰므로 OS 중립 → 향후 Windows 포팅 시 주력은 그대로.

## 2. 아키텍처

```
          게임 코드                         libmtd
   ┌───────────────────┐        ┌──────────────────────────────┐
   │ mtd_watch(&timer) │──등록─▶ │ 감시 테이블: {주소, 크기, 섀도,│
   │ mtd_begin("tick") │──구간─▶ │  태그, 규칙, 구간 플래그}      │
   │   timer++         │        │                               │
   │ mtd_end()         │──동기─▶ │ [예측] 구간 안 변화 규칙 검사   │
   │ mtd_event("input")│──사건─▶ │  + 섀도 동기화                 │
   └───────────────────┘        │                               │
                                │ 검사 스레드(주기 T):           │
   외부 변조 ─ /proc/mem ─┐      │ [행위] 구간 밖 실제≠섀도 탐지   │
            ─ ptrace ─────┴────▶ │  → 콜백 on_alert()            │
                                │                               │
                                │ (옵션) mprotect 백엔드:        │
                                │  내부 쓰기 SIGSEGV 포착        │
                                └──────────────────────────────┘
```

**탐지 2지점**
| 지점 | 시점 | 판단 | 탐지 종류 |
|---|---|---|---|
| `mtd_end` (동기) | 합법 구간 닫을 때 | 변화가 규칙에 맞나 | 예측 기반 (값) |
| 검사 스레드 (비동기, 주기 T) | 구간 밖 | 실제값 == 섀도인가 | 행위 기반 (타이밍) |

## 3. 공개 API (mtd.h v1 초안)

`extern "C"`, 경계 밖으로 예외·STL 금지 (`docs/plan.md` §2 이식성 원칙).

```c
/* 초기화 / 종료 */
int   mtd_init(const char *rules_path, mtd_alert_cb on_alert, const mtd_config *cfg);
void  mtd_shutdown(void);

/* 감시 전용 페이지에 변수 배치 (mprotect 보조 백엔드용; 섀도만 쓰면 선택) */
void *mtd_alloc(size_t size);

/* 감시 등록: 등록 시 섀도 = 현재 실제값 */
int   mtd_watch(void *addr, size_t size, const char *name);

/* 감시 해제 (11-11 결정): 반환 후 검사 스레드는 addr를 더 읽지 않는다(락으로 동기화).
   감시 메모리를 해제하기 전에 반드시 호출. 등록 안 된 주소면 MTD_ERR. */
int   mtd_unwatch(void *addr);

/* mtd_alloc 영역 반환 (11-11 결정). 그 안의 감시는 먼저 mtd_unwatch할 것. */
void  mtd_free(void *p);

/* 합법 변경 구간 (RAII 래퍼는 mtd.hpp의 mtd::Scope) */
void  mtd_begin(const char *tag);   /* 이 구간 안 변화는 '합법' */
void  mtd_end(void);                /* 규칙 검사 후 섀도 동기화 */

/* 행위 판단용 사건 통지 (예: "input" 직후에만 칸 상태가 바뀔 수 있음) */
void  mtd_event(const char *name);

/* 알림 콜백 */
typedef void (*mtd_alert_cb)(const mtd_alert *a);

typedef struct {
    const char *name;        /* 감시 변수 이름 */
    const char *kind;        /* "behavior" | "prediction" */
    const char *detail;      /* 위반 요약 (규칙 id 등) */
    uint64_t    shadow;      /* 기대값(섀도) */
    uint64_t    actual;      /* 실제값 */
    uint64_t    detect_ns;   /* CLOCK_MONOTONIC */
} mtd_alert;

typedef struct {
    unsigned period_ms;      /* 검사 주기 T. 기본 50ms (period-sweep.md로 확정) */
    int      enable_mprotect;/* 보조 백엔드 on/off. 기본 0 */
} mtd_config;
```

**설계 메모**
- **검사 주기 T가 탐지 지연을 지배.** 주기 스윕(`docs/poc/period-sweep.md`) 결과 max 지연 ≈ T+5ms, CPU는 1000변수에도 0.06~0.21%로 무시 가능 → 제약은 지연. 목표 100ms를 2배 마진으로 지키는 **T = 50ms 확정**(max 54ms).
- 변수 크기: v1은 `uint64_t` 이하 스칼라 우선. 지뢰 배치(배열)·칸 상태(배열)는 **바이트 비교**(11-11 결정): 섀도에 배열 전체 사본을 두고 `memcmp`, 차이가 나면 요소별로 규칙 검사(`per_element_monotonic`). 해시는 alert의 `expected/actual` 표현이 필요할 때만 계산. 배열 검사 오버헤드는 미측정(S2에서 측정).
- 감시 대상 수명(11-11 결정): 감시 메모리가 재할당되는 프로그램을 지원한다. 사용자는 해제 직전 `mtd_unwatch`, 새 영역 할당 직후 `mtd_watch`를 부른다. 지뢰찾기는 판마다 `GameState`를 새로 할당한다(PR #10).
- 콜백은 검사 스레드에서 호출 → 사용자 콜백 안에서 무거운 작업 금지 (문서화).

## 4. 탐지 로직

의사코드:
```
검사 스레드 (매 T ms):
  for v in watched:
    a = read_actual(v)
    if a != v.shadow:
      if v.in_window:            # 합법 구간 안 → mtd_end가 검사/동기화 담당
        continue
      elif v.needs_event and recent_event(v.event, within=T):
        v.shadow = a             # 사건 직후 합법 변경 → 수용
      else:
        alert(behavior, v, a)    # 구간 밖 변화 = 타이밍 위반
        v.shadow = a             # ack (다음 변화까지 재알림 억제)

mtd_end():
  for v changed in this window:
    if not rule_ok(v.tag, v.shadow, read_actual(v)):
      alert(prediction, v, ...)  # 값 위반
    v.shadow = read_actual(v)    # 동기화
  v.in_window = false
```

## 5. 변수별 적용 (지뢰찾기 6종)

| 변수 | 합법 변경 | 탐지 방식 |
|---|---|---|
| 타이머 | `tick` 구간 안 +1 | 구간 밖 변화 = 행위 / +1 아님 = 예측 |
| 지뢰 배치 | 첫 클릭 후 불변 (구간 없음) | **어떤 변화든 행위 위반** (섀도 고정) |
| 칸 상태 | `input` 사건 직후만 | 사건 없이 변화 = 행위 |
| 남은 지뢰 | 깃발 구간 안 ±1 | 구간 밖 = 행위 / ±1 아님 = 예측 |
| 열린 칸 수 | 칸 여는 구간 안, 단조 증가 | 구간 밖 / 감소 = 위반 |
| 게임 상태 | 승패 판정(`win-check`) 구간 안. 게임 코드는 칸 열기(`reveal`)와 승패 대입을 다른 구간으로 분리(11-11 결정) | 구간 밖 '승리'·'패배' = 행위 |

## 6. 플랫폼 격리 (Windows 고려)

지원 플랫폼은 **Linux**(계획서 §머리말). Windows는 개발 시 Docker/VM으로 통일. 단, 코드는 아래로 분리해 포팅 비용을 낮춘다:
- **주력(섀도·검사 스레드·규칙·콜백)**: 표준 C++17 + pthread → OS 중립.
- **보조(mprotect 백엔드)**: `monitor/backend_mprotect_posix.c` 로 격리. Windows면 `VirtualProtect`+VEH로 대체 구현 가능하게 인터페이스만 공유.
- `mtd_alloc`도 백엔드 뒤로 (POSIX `mmap` / Windows `VirtualAlloc`).

## 7. 한계

- 합법 구간이 **열린 순간** 끼어드는 변조는 `mtd_end` 동기화가 오염값을 수용할 수 있음 → 구간을 최소화하고 `mtd_end` 규칙 검사로 2차 방어. 완전 차단 아님 (은닉형, 계획서에서 한계로 보고 대상).
- 섀도는 **값 변화**만 본다. 정상 범위의 값으로 바꾸고 타이밍도 합법 구간에 맞춘 변조는 못 잡음 → 한계로 측정·보고.
- `ptrace` 변조는 대상을 멈춤 → 검사 스레드도 멈춤. 탐지는 재개 후 다음 주기. (지연 측정은 `/proc/mem` 기준)

## 8. 11-11 확정 전 결정할 것

> 11-11 PM 결정을 반영했다. `mtd.h` v1 확정 전 우진·전유정 확인 필요.

- [x] 검사 주기 T 기본값 → **50ms 확정** (`docs/poc/period-sweep.md`: max 지연 54ms로 100ms 목표에 2배 마진, CPU 0.12%/1000변수로 무시 가능. 제약은 CPU가 아니라 지연, T≤75ms면 지연 목표 충족)
- [x] 배열형 변수(지뢰 배치·칸 상태) 비교 방식 → **바이트 비교**. 해시도 전 바이트를 읽어야 해서 CPU 이득이 거의 없고, 칸 상태의 "열린 칸은 안 닫힘"(요소별 단방향) 규칙은 요소별 비교로만 검사 가능. 해시는 alert 표현용으로만. 오버헤드는 미측정(S2 측정).
- [x] `mtd_alloc` 필수 여부 → **선택**. 섀도만 쓰면 일반 변수도 `mtd_watch` 가능. `enable_mprotect=1`일 때만 보조 백엔드가 `mtd_alloc` 영역을 요구하고, 그 밖의 변수는 보조 백엔드가 건너뛴다(`docs/interface.md` §5와 일치).
- [x] 규칙 파일의 "변수-태그-사건" 매핑 표현 → **현행 변수 중심 유지**(변수마다 `behavior.allowed_in_tags`/`required_event`/`event_window_ms`). 검사 엔진의 변수 단위 검사와 1:1. `required_event` 의미론은 `docs/rules-format.md` 열린 질문으로 남김.
- [x] 콜백 재알림 억제(ack) → **라이브러리가 처리**. alert 직후 섀도 = 실제값(§4 의사코드). 변화 1건당 alert 1건이라 채점이 단순하고 알림 폭주가 없다.
- [x] 감시 대상 생애주기 → **주소 기반 `mtd_unwatch(addr)` + `mtd_free(p)` 추가**(§3). 근거: T1 게임(PR #10)은 판마다 `GameState`를 새로 할당·해제한다(봇 10분 실행 ≈ 3,470만 판, 2026-11-11 측정). 해제 API가 없으면 ① 해제된 주소 검사(오탐·정의되지 않은 동작) ② 감시 테이블 무한 증가 ③ `mtd_alloc` 누수. 실제 프로그램도 객체를 재할당하므로 게임에서 주소를 고정하지 않고 **라이브러리가 대응**한다.
- [x] 주입기(T2)의 대상 주소 확보 → **게임이 주소를 통지**(메모리 스캔은 구현하지 않음). 게임은 `GameState` 수명 지점 한 곳에서 같은 주소 정보를 **libmtd와 주입기 양쪽**에 넘긴다:
  - 생성 직후: 감시 변수마다 `mtd_watch` → 주소 로그에 `state_alloc` 기록
  - 해제 직전: 주소 로그에 `state_free` 기록 → 감시 변수마다 `mtd_unwatch` → 메모리 해제
  - 주소 로그 형식과 채점 규칙은 `docs/interface.md` §3.1·§4.

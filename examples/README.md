# examples (T4)

이식성 검증용 두 번째 앱: **2048.c** ([mevdschee/2048.c](https://github.com/mevdschee/2048.c), MIT).

## PM 결정 (2026-11-16)
두 번째 앱은 오픈소스 터미널 게임 2048.c로 한다. (원안: 인벤토리/잔고 앱, 검토 후보: 둠·nInvaders)

**근거**
- C 파일 1개(577줄), 의존성은 C 컴파일러뿐 → 기존 Docker 이미지(gcc:13)·CI에서 그대로 빌드
- 감시 변수가 분명함: `board[4][4]`(칸), `score`(점수) — 둘 다 `main` 지역 변수
- 쓰기 지점이 `main` 루프의 `moveX()`·`addRandom()`·재시작에 모여 있어 수정량이 "20줄 이하" 목표 안에 들 것으로 예상 (예상치, 미측정)
- 행위 기반(입력 없이 칸 변경)·예측 기반(점수 감소, 합쳐진 타일 값과 다른 증가) 둘 다 적용 가능
- 입력을 `getchar()`로 받으므로 무작위 `wasd`를 파이프로 넣어 자동 플레이 가능 (S4-04에서 확인)

**검토 후 뺀 후보**
- 둠: 체력·탄약 쓰기 지점이 여러 곳에 흩어져 20줄 기준을 넘기기 쉽고, SDL 의존성·일정(4주차 1주, PM 단독) 위험
- nInvaders(GPL-2): 실시간 변수(타이머형)는 장점이나 ncurses 의존·다중 파일이라 일정상 2048.c가 안전

## 적용 방식
- 원본은 저장소에 복사하지 않는다(`AGENTS.md` 서드파티 규칙). CMake `FetchContent`로 **고정 커밋**을 받아 오고, libmtd 적용분은 **패치 파일**로만 둔다 → 패치 줄 수가 곧 "코드 수정량" 측정값
- 적용은 `docs/interface.md`·`libmtd/include/mtd.h` 문서만 보고 한다(이식성 검증 조건)

## 구성 (S4-04)
| 파일 | 내용 |
|---|---|
| `CMakeLists.txt` | 고정 커밋 `90343eb2`를 `FetchContent`로 받아 `build/examples/2048-patched/`에 복사하고 `git apply`로 패치 적용 → `build/examples/2048`. 원본은 서드파티라 `-Werror`에서 뺀다. 구성 때 네트워크가 필요하고, 끄려면 `-DMTD_EXAMPLE_2048=OFF` |
| `2048/mtd.patch` | 원본 수정 전부(= 이식 수정량) |
| `2048/rules.json` | 규칙 파일. `score`: `move`·`restart` 안에서만, 단조 증가 |
| ctest `example_2048` | 원본 자체 테스트(`2048 test`)로 패치 후 게임 규칙이 그대로인지 확인 |
| `2048/autoplay.py` | 자동 플레이: 무작위 `wasd`를 파이프로 넣고, 게임이 끝나면 새 프로세스로 다시 시작. stderr의 alert JSONL을 모은다 |

**패치 내용**: `#include "mtd.h"`, `initBoard` 뒤에 `mtd_init(getenv("MTD_RULES"), NULL)`와 `board`·`score` `mtd_watch`, 키 처리 `switch`를 `move` 구간으로, `addRandom`을 `spawn` 구간으로 감쌌다. 재시작은 `restart` 구간에서 `score`를 감시 해제 → 0 → 다시 감시(점수 감소가 예측 위반이 되지 않게), 끝에 `mtd_shutdown`. 콜백 없이 초기화해서 alert는 libmtd 기본 동작대로 stderr에 `interface.md` §2 JSONL로 나온다.

**이식하며 찾은 것**
- `board`는 규칙 형식의 예측 규칙 6종 중 맞는 것이 없다. 타일이 움직이고 합쳐지므로 요소별 단조·불변·변화량이 모두 맞지 않는다. 그래서 규칙 파일에 선언하지 않았다. 규칙 없는 변수는 어느 구간이든 합법이고 구간 밖 변화만 behavior alert가 된다(`docs/rules-format.md`). 그 대가로 `mtd_watch` 때 stderr에 경고가 1회 나온다. "예측 규칙 없음"을 규칙 파일에서 명시할 방법이 없다는 점은 규칙 형식의 한계로 보고한다.
- 판(프로세스)마다 `mtd_init`/`mtd_shutdown`을 하므로 판 교체 때 감시 수명 문제가 없다. 지뢰찾기와 달리 `board`·`score`가 `main` 지역 변수라 주소가 판 동안 고정이다.
- 외부 변조 확인(1회, gdb로 `score = 123456`, `board[0][0] = 9`): 두 변수 모두 behavior alert(`score`는 `rule="allowed_in_tags"`, `board`는 `rule=null`). 2026-11-23 Docker에서 수동으로 확인했다. 정식 탐지율은 S4-05에서 잰다.

## 측정 항목
| 항목 | 목표 | 결과 |
|---|---|---|
| 코드 수정량 | 20줄 이하 | **13줄 추가, 기존 줄 변경 0** (`2048/mtd.patch`, S4-04) |
| 오탐률 (정상 자동 플레이) | 지뢰찾기와 같은 기준 | **10분 FP 0건**: 27판(게임 오버 26), 입력 3,695회, 크래시 0 (`results/2026-11-23_2048-normal_1/`, Docker Desktop macOS, 1회). 오탐률의 분모인 `mtd_end` 수는 2048 쪽에서 세지 않아 비율 대신 건수로 적는다 |
| 탐지율 | 지뢰찾기와 같은 기준 | 미측정 (S4-05) |

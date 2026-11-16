# game/AGENTS.md — T1 지뢰찾기 + 자동 플레이 봇

루트 `AGENTS.md`를 먼저 읽는다. 이 문서는 `game/`에서만 필요한 맥락과 주의점이다.

## 이 모듈이 하는 일
libmtd의 첫 사용처이자 실험 대상 프로그램. 시드 기반 터미널 지뢰찾기와 화면 없는 봇(`--bot`, 안전 칸만 무작위로 연다).
담당: 전유정. libmtd 연결(통합, S2-06)은 PM.

## 파일
- `minesweeper.hpp/.cpp` — 게임 규칙과 `GameState`(감시 대상 변수가 모인 구조체)
- `main.cpp` — 대화형·봇 실행, 명령줄 옵션
- `rules.json` — 이 게임의 변수 규칙(libmtd 규칙 파일). 형식은 `docs/rules-format.md`
- `tests/` — doctest

## 감시 대상 변수 (6종)
`timer`, `mines_remaining`, `mine_field`, `cell_state`, `cells_opened`, `game_state`.
이름은 `rules.json`·`mtd_watch`·주소 로그·정답 라벨에서 **같은 키**다. 바꾸면 네 곳을 같이 바꾼다.

## 꼭 지킬 것
- **값 변경은 구간 안에서만**: 감시 변수를 바꾸는 코드는 해당 구간(`tick`·`flag`·`reveal`·`win-check`) 안에 있어야 한다. 구간 밖 대입은 곧 오탐이다. 칸 열기(`reveal`)와 승패 대입(`win-check`)은 다른 구간(11-11 결정).
- **수명 순서**(`docs/interface.md` §3.1): 생성 직후 5개 `mtd_watch` → `state_alloc` 기록 / 지뢰 배치 직후 `mine_field` `mtd_watch` → `field_watch` 기록 / `state_free` 기록 → `mtd_unwatch` → 해제. 순서를 바꾸면 주입기·채점이 틀어진다.
- **게임 지식은 여기에**: 변수 규칙을 libmtd 코드로 옮기지 않는다. `rules.json`을 고친다.
- 오탐률 측정용 봇 실행은 `--speed-ms`를 줘서 한 판이 수 초 이상 이어지게 한다(`--speed-ms 0`이면 `timer`가 한 번도 오르지 않음). 측정 조건은 `docs/experiment.md`(S2-05).

## 확인 방법
```bash
scripts/dev.sh build
./build/game/minesweeper --bot --seed 42 --speed-ms 5 --duration-sec 600
```

# game (T1)

시드 기반 지뢰찾기와 화면 없는 안전 칸 자동 플레이 봇.

## 빌드·실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/game/minesweeper
./build/game/minesweeper --difficulty hard
./build/game/minesweeper --bot --seed 42 --speed-ms 5 --duration-sec 600
```

대화형 조작은 `w/a/s/d`로 이동하고 `o`로 열기, `f`로 깃발을 놓거나
회수합니다. 승리·패배 후 `n`으로 새 판을 시작할 수 있습니다. 자동 플레이는
지뢰 배치를 알고 안전한 칸만 무작위로 열며, 종료 시 새 판을 시작합니다.
`--duration-sec 0`(기본값)은 중단할 때까지 실행합니다.

감시 대상 상태는 `GameState`에 모여 있습니다. 보드는 최대 30×16이며,
`StateAllocator` 콜백을 통해 상태 구조체 할당기를 교체할 수 있습니다.

## 후속 작업 (PR #10 리뷰에서 나온 것)

- **libmtd 연동**: 판마다 `GameState`를 새로 할당하므로, 감시 해제 API가 생기면
  `Game` 소멸자에서 해제해야 한다. API 결정은 `docs/monitoring-design.md` §8.
- **`lost` 전환 태그**: 규칙과 코드가 어긋남. `docs/rules-format.md` 열린 질문 참고.
- **정상 동작 데이터 수집 시 봇 속도**: `--speed-ms 0`이면 한 판이 1초 안에 끝나
  `timer`가 한 번도 오르지 않는다(10분 실행 시 `timer=0s`). 오탐률 측정용 실행은
  한 판이 수 초 이상 이어지도록 `--speed-ms`를 준다.
- **터미널 UI 개선(선택)**: 키마다 Enter 필요, 타이머 표시는 입력 때만 갱신,
  패배 시 지뢰 위치 미표시.
- **정리(선택)**: `run_interactive`의 도달 불가 `q` 분기와 승패 처리 중복, `--help`에
  `n`(새 판) 키 설명 누락.

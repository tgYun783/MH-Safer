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

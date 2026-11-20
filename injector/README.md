# injector (T2)

변조 주입기. 게임 주소 로그(`docs/interface.md` §3.1)를 따라 대상 주소를 갈아타며 `/proc/<pid>/mem`으로 쓰고, 변조 1건마다 정답 라벨 JSONL(§3)을 남긴다. 메모리 스캔은 하지 않는다. PoC는 `docs/poc/`.

## 실행 (S3-11 최소 버전)
```bash
./build/game/minesweeper --bot --difficulty hard --seed 1 --speed-ms 50 --duration-sec 600 \
  --addr-log results/<run>/addr.jsonl --alert-log results/<run>/alerts.jsonl --stats results/<run>/stats.json &
./build/injector/mtd_injector --pid $! --addr-log results/<run>/addr.jsonl \
  --labels results/<run>/labels.jsonl --scenario T-timer --count 100
```
| 옵션 | 기본 | 뜻 |
|---|---|---|
| `--scenario` | (필수) | `<유형>-<변수>`(`docs/experiment.md` §4.1, §7) |
| `--count` | 100 | 라벨 수 목표 |
| `--interval-ms` / `--jitter-ms` | 500 / 200 | 변조 간격 = interval + 0~jitter 무작위(§4.2) |
| `--duration-sec` | 0 | 최대 실행 시간(0 = 제한 없음). 대상이 끝나도 멈춘다 |
| `--seed` | 1 | 지터·무작위 선택 시드 |
| `--no-freeze` | — | 쓰기 동안 대상 멈춤을 끈다(해제된 메모리에 쓸 위험, 시험용) |
| `--stats FILE` | — | 실행 요약 JSON(라벨 수, `freed_writes` 등) |

`labels` 파일이 이미 있으면 거부한다(원본 데이터 보호). 종료 코드는 쓰기 실패나 해제된 주소 쓰기가 있으면 1.

## 지원 시나리오 (S3-12: `docs/experiment.md` §4.1 표의 "—"가 아닌 14칸 전부)
| 시나리오 | 쓰는 값 |
|---|---|
| `T-timer` / `T-cells_opened` | +1 |
| `T-mines_remaining` | ±1 |
| `T-cell_state` | 닫힌 안전 칸 1개 열기 (`mine_field`가 알려진 뒤) |
| `V-timer` | 0 |
| `V-mines_remaining` | 999 |
| `V-cells_opened` | +100 |
| `V-game_state` | 1(won), playing일 때만 |
| `V-mine_field` | 지뢰 1개 제거 |
| `V-cell_state` | 열린 칸 1개 닫기 |
| `H-timer` / `H-cells_opened` | 정상 변경 직후 +1 (`hidden=true`) |
| `H-mines_remaining` | 정상 변경 직후 ±1 (`hidden=true`) |
| `H-cell_state` | 정상 변경 직후 닫힌 안전 칸 1개 열기 (`hidden=true`) |

**H(은닉형) 동작**: 간격이 지나면 대상 변수를 0.2ms마다 읽어(대상은 멈추지 않음) 값이 바뀌는 순간 = 게임의 정상 변경(`tick`·`flag`·`reveal`) 직후를 잡고, 곧바로 아래 멈춤 → 쓰기를 한다. 5초 동안 변화가 없으면 다시 기다린다(`h_timeouts`). 정상 변경을 본 뒤 쓰기까지의 지연은 `--stats`의 `h_delay_avg_ms`·`h_delay_max_ms`.

**칸 위치**: `cell_state`의 T·H는 §4.1의 "이웃 칸" 대신 무작위 닫힌 안전 칸을 연다(주소 로그에 보드 폭이 없음, 탐지 규칙은 위치와 무관). 보드가 배열 전체(480칸)를 채우는 `hard` 기준이다.

## 해제된 메모리에 쓰지 않는 방법
쓰기 직전에 대상을 `SIGSTOP`으로 멈추고(모든 스레드 정지 확인), 주소 로그를 끝까지 다시 읽은 뒤 쓰고 `SIGCONT`로 재개한다. 게임은 `state_free`를 기록·flush한 다음 메모리를 해제하므로, 멈춘 상태에서 `state_free`가 보이지 않으면 그 상태는 아직 살아 있다. 사후 확인으로 라벨마다 그 수명의 `state_free` 시각과 비교해 `freed_writes`를 센다(0이어야 함). 멈춤 시간은 쓰기 1건에 수십 µs 수준이다(미측정).

**주의 (사람 플레이·시연)**: 게임을 셸의 **포그라운드**에서 띄우고 다른 터미널에서 주입기를 돌리면, 셸이 SIGSTOP을 보고 게임을 "Stopped" 작업으로 바꾸고 터미널을 가져간다(SIGCONT 뒤에도 백그라운드로 남아 입력을 못 받는다). 사람이 직접 플레이하는 시연에서는 `--no-freeze`를 쓰고, 해제 직후 쓰기 위험은 감수한다. 실험(봇을 `&`로 실행)에는 해당 없다.

## 권한
`/proc/<pid>/mem` 쓰기는 대상의 부모 프로세스이거나 `SYS_PTRACE`가 필요하다(`scripts/dev.sh`가 부여). 테스트(`test_injector`)는 게임을 자식으로 띄워 Yama `ptrace_scope=1`에서도 돈다.

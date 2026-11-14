@AGENTS.md

# Claude 전용 안내

위 `AGENTS.md`가 공통 지침이다(Claude Code·Codex 공용). 아래는 Claude Code에서 작업할 때의 추가 안내.

## 작업 흐름
- 작업 전 `docs/sprints/`에서 이번 스프린트 백로그(해당 `S<n>-xx` 항목)의 **완료 기준**을 확인하고, 끝나면 그 기준을 실제로 돌려 본다.
- 코드 변경은 **새 브랜치 + PR**로 올린다. `main` 직접 커밋 금지. 브랜치 이름은 `s<스프린트>/<작업>` 형식(예: `s1/skeleton`).
- 서로 의존하는 작업은 스택 PR로 올리고, PR 설명에 **머지 순서**를 밝힌다.
- 빌드·테스트는 로컬에 환경이 없으면 `AGENTS.md`의 Docker 명령으로 확인한다.

## 스킬 (`.claude/skills/`)
| 스킬 | 언제 | 하는 일 |
|---|---|---|
| `build-test` | "빌드하고 테스트 돌려줘", PR 전 확인 | `scripts/dev.sh build`(Docker 빌드+ctest) 실행 후 결과 보고 |

Codex 등 다른 도구는 같은 스크립트를 직접 실행한다(`AGENTS.md` 빌드·테스트 명령).

## 이 저장소의 결정된 방향 (바꾸려면 근거 필요)
- 감시 **주력 = 섀도 복사+주기 검사**, mprotect는 **내부 쓰기 보조**. 근거: `docs/poc/write-paths.md`(외부 변조는 SIGSEGV를 안 냄).
- 공개 API는 `extern "C"` 경계 유지, 섀도 엔진은 OS 중립으로 짜서 mprotect 등 플랫폼 의존 코드만 백엔드로 격리.

## 하지 말 것
- `docs/plan.md`·`docs/sprints/`의 확정 사항을 임의로 바꾸지 않는다(결정은 팀 회의).
- 실험 결과를 추측으로 채우지 않는다. 측정 안 한 수치는 "미측정"으로 둔다.

---
name: build-test
description: MH-Safer 저장소를 Docker 개발 컨테이너에서 빌드하고 ctest를 돌린다. "빌드해줘", "테스트 돌려줘", "빌드하고 테스트", "CI처럼 확인", PR 올리기 전 확인, 완료 기준 확인에 사용. 스크립트 scripts/dev.sh build를 부르기만 한다.
---

# build-test

실제 작업은 `scripts/dev.sh build`가 한다(S1-02). 이 스킬은 그것을 부르고 결과를 보고만 한다. 새 스크립트를 만들지 않는다.

## 절차
1. 저장소 루트에서 실행한다:
   ```bash
   scripts/dev.sh build 2>&1 | tail -60
   ```
   - 컨테이너 이미지(`mh-safer-dev`)를 빌드하고 `cmake` 구성 → 빌드 → `ctest --output-on-failure`를 한 번에 돈다.
   - 첫 실행은 이미지 빌드로 몇 분 걸린다. Bash 타임아웃을 10분으로 준다.
   - 특정 테스트만: `scripts/dev.sh bash -lc 'cmake --build build -j && ctest --test-dir build -R <이름> --output-on-failure'`
2. 보고: 통과/실패 수(`N% tests passed, M tests failed out of K`)를 그대로 옮기고, 실패면 실패한 테스트 이름과 출력 핵심 몇 줄을 붙인다. 컴파일 오류면 첫 오류의 `파일:줄`과 메시지.
3. Docker가 안 떠 있으면(`Cannot connect to the Docker daemon`) 사용자에게 Docker Desktop을 켜 달라고 한다. 로컬 cmake로 대신 빌드하지 않는다(팀 환경과 달라짐).

## 하지 말 것
- 실패를 숨기거나 "대부분 통과"처럼 뭉개지 않는다.
- 테스트를 통과시키려고 테스트 코드를 고치지 않는다(고칠 필요가 있으면 먼저 사용자에게 말한다).

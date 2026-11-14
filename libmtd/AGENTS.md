# libmtd/AGENTS.md — 감시 라이브러리

루트 `AGENTS.md`를 먼저 읽는다. 이 문서는 `libmtd/`에서만 필요한 맥락과 주의점이다.

## 이 모듈이 하는 일
게임 등 사용 프로그램에 링크되는 메모리 변조 탐지 라이브러리. 탐지 2지점:
- **검사 스레드(주기 T, 기본 50ms)**: 구간 밖에서 실제값 ≠ 섀도 → behavior alert (행위 기반)
- **`mtd_end`(동기)**: 구간 안 변화가 규칙에 맞는지 → prediction alert (예측 기반)

설계 근거는 `docs/monitoring-design.md`, 데이터 형식은 `docs/interface.md`, 규칙 파일은 `docs/rules-format.md`.

## 담당
| 계층 | 내용 | 담당 |
|---|---|---|
| L1·L2 | 공개 API, 감시 테이블, 검사 스레드, `mtd_alloc`/`mtd_free` | 우진 (12-01부터 PM 유지보수) |
| L3 | 행위 탐지: 구간·사건·ack | 우진 (12-01부터 PM 유지보수) |
| L4·L5 | 예측 규칙 검사, 규칙 파서, 판정 결합 | 전유정 |

모듈 경계를 넘는 내부 인터페이스(규칙 조회·예측 검사 호출 지점)는 `src/` 내부 헤더로 정하고, 바꿀 때는 두 담당자가 함께 승인한다(S2-13).

## 파일
- `include/mtd.h` — 공개 C API. **v1 기준 문서는 이 헤더**다(설계 문서와 다르면 헤더가 맞다).
- `include/mtd.hpp` — C++ RAII 래퍼(`mtd::Session`, `mtd::Scope`). 헤더 안에서만 C++ 사용.
- `src/` — 구현. 플랫폼 의존 코드(mmap, mprotect)는 별도 파일로 격리해 섀도 엔진은 OS 중립으로 둔다.
- `tests/` — doctest 단위 테스트. 테스트 하나 = 실행 파일 하나 + `add_test`.

## 꼭 지킬 것
- **API 경계**: `extern "C"` 함수 밖으로 C++ 예외가 나가지 않게 모든 진입점에서 잡아 `MTD_ERR`로 바꾼다. nlohmann/json 파싱 예외도 마찬가지. STL 타입을 공개 헤더(`mtd.h`)에 넣지 않는다.
- **게임 지식 금지**: 변수 이름·태그(`tick`, `reveal` 등)를 코드에 하드코딩하지 않는다. 전부 `rules.json`에서 온다. 테스트에서만 예시 이름 사용.
- **감시 수명**: `mtd_unwatch(addr)` 반환 뒤에는 검사 스레드가 그 주소를 절대 읽지 않는다(락으로 보장). 사용 프로그램은 판마다 상태를 재할당한다(10분에 수천만 회) → 등록·해제 경로는 누수·증가 없이 가볍게.
- **콜백**: `on_alert`는 검사 스레드 문맥에서 호출된다. 콜백 호출 중 엔진 락을 잡고 있으면 콜백이 API를 다시 부를 때 교착된다 — 락 밖에서 호출한다.
- **시그널 핸들러**(mprotect 보조 백엔드): async-signal-safe 코드만. 할당·예외·락·`std::` 컨테이너 금지, 미리 할당한 고정 버퍼에만 기록.
- **수치**: 성능·지연 수치는 측정한 것만 문서에 쓴다. 측정 안 했으면 "미측정".

## 확인 방법
```bash
scripts/dev.sh build                                   # Docker 빌드 + 전체 ctest
ctest --test-dir build -R <테스트이름> --output-on-failure
```
스레드·수명 관련 변경은 ASan/TSan 빌드로도 돌려 본다(`-DCMAKE_CXX_FLAGS=-fsanitize=address` 등).

## 인수인계 (11-30까지, 우진 → PM)
이 문서를 최신으로 유지한다: 구조, 알려진 한계, 자주 나는 버그와 대처. 열린 이슈는 GitHub Issues에.

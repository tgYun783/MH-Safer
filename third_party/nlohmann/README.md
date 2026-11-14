# nlohmann/json (vendored)

- 버전: v3.11.3
- 출처: https://github.com/nlohmann/json (MIT License), 단일 헤더 `single_include/nlohmann/json.hpp`
- 단일 헤더만 포함 (AGENTS.md: 헤더 전용 단일 파일). 수정하지 않는다.
- 사용: `target_include_directories(<타깃> SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)` 후 `#include <nlohmann/json.hpp>`
- 주의: 파싱 오류는 C++ 예외(`nlohmann::json::parse_error`)다. libmtd 안에서 쓰면 `extern "C"` 경계를 넘기 전에 잡아서 `MTD_ERR`로 바꾼다.

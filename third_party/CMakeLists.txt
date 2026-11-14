# 헤더 전용 단일 파일 벤더링 (AGENTS.md 의존성 규칙).
# 사용하는 타깃은 다음처럼 SYSTEM include로 경고에서 뺀다:
#   target_include_directories(<타깃> SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
#   #include <nlohmann/json.hpp>

# S2-08: -Wall -Wextra -Werror 아래에서 <nlohmann/json.hpp>가 빌드되는지 확인
add_executable(test_third_party_json tests/test_json.cpp)
target_include_directories(test_third_party_json SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/doctest)
add_test(NAME third_party_json COMMAND test_third_party_json)

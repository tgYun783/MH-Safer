# doctest 단일 헤더는 third_party/doctest 에 두고 SYSTEM include로 경고 제외.
add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE mtd)
target_include_directories(test_smoke SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest)

add_test(NAME smoke COMMAND test_smoke)

add_executable(test_rules test_rules.cpp)
target_link_libraries(test_rules PRIVATE mtd)
target_include_directories(test_rules SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest)
target_include_directories(test_rules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_include_directories(test_rules SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(test_rules PRIVATE
  MTD_TEST_RULES_PATH="${CMAKE_SOURCE_DIR}/game/rules.json")

add_test(NAME rules COMMAND test_rules)

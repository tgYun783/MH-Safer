# doctest 단일 헤더는 third_party/doctest 에 두고 SYSTEM include로 경고 제외.
set(MTD_DOCTEST_DIR ${CMAKE_SOURCE_DIR}/third_party/doctest)

add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE mtd)
target_include_directories(test_smoke SYSTEM PRIVATE ${MTD_DOCTEST_DIR})
add_test(NAME smoke COMMAND test_smoke)

# 내부 인터페이스 테스트: libmtd/src 헤더를 직접 include 한다(공개 API 아님).
add_executable(test_rules_iface test_main.cpp test_rules_iface.cpp)
target_link_libraries(test_rules_iface PRIVATE mtd)
target_include_directories(test_rules_iface PRIVATE ${CMAKE_SOURCE_DIR}/libmtd/src)
target_include_directories(test_rules_iface SYSTEM PRIVATE ${MTD_DOCTEST_DIR})
add_test(NAME rules_iface COMMAND test_rules_iface)

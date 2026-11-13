# doctest 단일 헤더는 third_party/doctest 에 두고 SYSTEM include로 경고 제외.
set(MTD_DOCTEST_DIR ${CMAKE_SOURCE_DIR}/third_party/doctest)
set(MTD_SRC_DIR ${CMAKE_SOURCE_DIR}/libmtd/src)  # 내부 헤더(공개 API 아님)

add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE mtd)
target_include_directories(test_smoke SYSTEM PRIVATE ${MTD_DOCTEST_DIR})
add_test(NAME smoke COMMAND test_smoke)

# S2-13 내부 인터페이스
add_executable(test_rules_iface test_main.cpp test_rules_iface.cpp)
target_link_libraries(test_rules_iface PRIVATE mtd)
target_include_directories(test_rules_iface PRIVATE ${MTD_SRC_DIR})
target_include_directories(test_rules_iface SYSTEM PRIVATE ${MTD_DOCTEST_DIR})
add_test(NAME rules_iface COMMAND test_rules_iface)

# S2-11 감시 엔진 코어
add_executable(test_engine test_main.cpp test_engine.cpp)
target_link_libraries(test_engine PRIVATE mtd)
target_include_directories(test_engine SYSTEM PRIVATE ${MTD_DOCTEST_DIR})
add_test(NAME engine COMMAND test_engine)

# S2-12 행위 탐지 (+ S2-13 의미론)
add_executable(test_behavior test_main.cpp test_behavior.cpp)
target_link_libraries(test_behavior PRIVATE mtd)
target_include_directories(test_behavior PRIVATE ${MTD_SRC_DIR})
target_include_directories(test_behavior SYSTEM PRIVATE ${MTD_DOCTEST_DIR})
add_test(NAME behavior COMMAND test_behavior)

# S2-11 "ASan 테스트에서 해제 후 접근 0건": 같은 엔진 테스트를 ASan 계측 라이브러리로.
if(TARGET mtd_asan)
  add_executable(test_engine_asan test_main.cpp test_engine.cpp)
  target_link_libraries(test_engine_asan PRIVATE mtd_asan)
  target_include_directories(test_engine_asan SYSTEM PRIVATE ${MTD_DOCTEST_DIR})
  add_test(NAME engine_asan COMMAND test_engine_asan)
  # 누수 검사(LSan)는 ptrace 권한 없는 컨테이너에서 실패하므로 끈다. 기준은 해제 후 접근.
  set_tests_properties(engine_asan PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")
endif()

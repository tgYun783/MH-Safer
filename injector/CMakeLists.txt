# T2 변조 주입기 (S3-11 최소 버전). Linux 전용(/proc/<pid>/mem, SIGSTOP).
add_library(injector_core
  src/address_tracker.cpp
  src/scenario.cpp
  src/injector.cpp)
target_include_directories(injector_core PUBLIC src)
target_include_directories(injector_core SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party)

add_executable(mtd_injector src/main.cpp)
target_link_libraries(mtd_injector PRIVATE injector_core)

# 단위 + 실제 게임 통합 테스트 (통합 2건, 합쳐 약 1~2분)
add_executable(test_injector tests/test_injector.cpp)
target_link_libraries(test_injector PRIVATE injector_core)
target_include_directories(test_injector SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest)
target_compile_definitions(test_injector PRIVATE
  INJECTOR_GAME_BIN="$<TARGET_FILE:minesweeper>"
  INJECTOR_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(test_injector minesweeper)
add_test(NAME injector COMMAND test_injector)
set_tests_properties(injector PROPERTIES TIMEOUT 300)

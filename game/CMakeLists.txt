add_library(minesweeper_core minesweeper.cpp address_log.cpp)
target_include_directories(minesweeper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(minesweeper_core SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

# libmtd 연결 (S3-02). 게임 코어는 libmtd에 의존하지 않고 StateObserver 훅만 낸다.
add_library(minesweeper_mtd mtd_glue.cpp)
target_link_libraries(minesweeper_mtd PUBLIC minesweeper_core mtd)

add_executable(minesweeper main.cpp)
target_link_libraries(minesweeper PRIVATE minesweeper_mtd)
target_compile_definitions(minesweeper PRIVATE
  MTD_GAME_RULES_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/rules.json")

add_executable(test_game tests/test_game.cpp)
target_link_libraries(test_game PRIVATE minesweeper_core)
target_include_directories(test_game SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest)
target_include_directories(test_game SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
add_test(NAME game COMMAND test_game)

# 게임 + 실제 libmtd + game/rules.json 통합 테스트 (S3-02 완료 기준)
add_executable(test_game_mtd tests/test_game_mtd.cpp)
target_link_libraries(test_game_mtd PRIVATE minesweeper_mtd)
target_include_directories(test_game_mtd SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest)
target_compile_definitions(test_game_mtd PRIVATE
  MTD_GAME_RULES="${CMAKE_CURRENT_SOURCE_DIR}/rules.json"
  MTD_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME game_mtd COMMAND test_game_mtd)

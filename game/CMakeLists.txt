add_library(minesweeper_core minesweeper.cpp address_log.cpp)
target_include_directories(minesweeper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(minesweeper_core SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)

add_executable(minesweeper main.cpp)
target_link_libraries(minesweeper PRIVATE minesweeper_core)

add_executable(test_game tests/test_game.cpp)
target_link_libraries(test_game PRIVATE minesweeper_core)
target_include_directories(test_game SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest)
target_include_directories(test_game SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
add_test(NAME game COMMAND test_game)

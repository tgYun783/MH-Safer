#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "minesweeper.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>

namespace {

alignas(minesweeper::GameState) std::byte state_buffer[sizeof(minesweeper::GameState)];

void *test_allocate(std::size_t size)
{
    return ::operator new(size);
}

void *pool_allocate(std::size_t)
{
    return state_buffer;
}

void test_deallocate(void *memory)
{
    ::operator delete(memory);
}

}  // namespace

TEST_CASE("난이도별 보드 설정")
{
    const auto easy = minesweeper::config_for(minesweeper::Difficulty::easy);
    const auto medium = minesweeper::config_for(minesweeper::Difficulty::medium);
    const auto hard = minesweeper::config_for(minesweeper::Difficulty::hard);
    CHECK(easy.rows == 9);
    CHECK(easy.columns == 9);
    CHECK(easy.mines == 10);
    CHECK(medium.rows == 16);
    CHECK(medium.columns == 16);
    CHECK(medium.mines == 40);
    CHECK(hard.rows == 16);
    CHECK(hard.columns == 30);
    CHECK(hard.mines == 99);
}

TEST_CASE("잘못된 보드 설정은 거부한다")
{
    CHECK_THROWS_AS(minesweeper::Game({0, 4, 1}, 1), std::invalid_argument);
    CHECK_THROWS_AS(minesweeper::Game({2, 2, 4}, 1), std::invalid_argument);
    CHECK_THROWS_AS(minesweeper::Game({31, 16, 1}, 1), std::invalid_argument);
    CHECK_THROWS_AS(minesweeper::Game({2, 2, 1}, 1, {nullptr, test_deallocate}),
                    std::invalid_argument);
}

TEST_CASE("첫 클릭은 안전하고 지뢰 수가 설정과 일치한다")
{
    minesweeper::Game game({5, 5, 6}, 42);
    CHECK(game.open(2, 2));
    CHECK(game.state().mines_placed);
    CHECK(game.state().mine_field[2 * 5 + 2] == 0);

    std::uint32_t mine_count = 0;
    for (std::size_t cell = 0; cell < 25; ++cell) {
        mine_count += game.state().mine_field[cell];
    }
    CHECK(mine_count == 6);
}

TEST_CASE("빈 영역을 열면 인접한 안전 칸도 연쇄로 열린다")
{
    bool found_flood = false;
    for (std::uint32_t seed = 0; seed < 100 && !found_flood; ++seed) {
        minesweeper::Game game({5, 5, 1}, seed);
        REQUIRE(game.open(0, 0));
        if (game.adjacent_mines(0, 0) == 0) {
            CHECK(game.state().cells_opened > 1);
            found_flood = game.state().cells_opened > 1;
        }
    }
    CHECK(found_flood);
}

TEST_CASE("지뢰를 열면 패배한다")
{
    minesweeper::Game game({5, 5, 3}, 15);
    REQUIRE(game.open(0, 0));
    for (std::uint32_t row = 0; row < 5; ++row) {
        for (std::uint32_t column = 0; column < 5; ++column) {
            const std::size_t cell = row * 5 + column;
            if (game.state().mine_field[cell] != 0) {
                CHECK(game.open(row, column));
                CHECK(game.state().game_state == minesweeper::GameStatus::lost);
                return;
            }
        }
    }
    FAIL("the board should contain mines");
}

TEST_CASE("모든 안전 칸을 열면 승리한다")
{
    minesweeper::Game game({5, 5, 5}, 11);
    REQUIRE(game.open(0, 0));
    for (std::uint32_t row = 0; row < 5; ++row) {
        for (std::uint32_t column = 0; column < 5; ++column) {
            const std::size_t cell = row * 5 + column;
            if (game.state().mine_field[cell] == 0) {
                game.open(row, column);
            }
        }
    }
    CHECK(game.state().cells_opened == 20);
    CHECK(game.state().game_state == minesweeper::GameStatus::won);
}

TEST_CASE("깃발은 카운터를 조정하고 숨겨진 칸에만 놓을 수 있다")
{
    minesweeper::Game game({4, 4, 2}, 7);
    CHECK(game.state().mines_remaining == 2);
    CHECK(game.toggle_flag(1, 1));
    CHECK(game.state().mines_remaining == 1);
    CHECK(game.toggle_flag(1, 1));
    CHECK(game.state().mines_remaining == 2);
    CHECK(game.open(0, 0));
    CHECK_FALSE(game.toggle_flag(0, 0));
}

TEST_CASE("깃발이 꽂힌 칸은 열리지 않는다")
{
    minesweeper::Game game({4, 4, 2}, 13);
    REQUIRE(game.toggle_flag(0, 0));
    CHECK_FALSE(game.open(0, 0));
    CHECK_FALSE(game.state().mines_placed);
    CHECK(game.state().cells_opened == 0);
}

TEST_CASE("타이머는 첫 클릭 이후 게임 중에만 증가한다")
{
    minesweeper::Game game({4, 4, 2}, 19);
    game.tick();
    CHECK(game.state().timer_seconds == 0);
    REQUIRE(game.open(0, 0));
    game.tick();
    game.tick();
    CHECK(game.state().timer_seconds == 2);

    for (std::uint32_t row = 0; row < 4; ++row) {
        for (std::uint32_t column = 0; column < 4; ++column) {
            const std::size_t cell = row * 4 + column;
            if (game.state().mine_field[cell] != 0) {
                game.open(row, column);
                game.tick();
                CHECK(game.state().game_state == minesweeper::GameStatus::lost);
                CHECK(game.state().timer_seconds == 2);
                return;
            }
        }
    }
    FAIL("the board should contain mines");
}

TEST_CASE("같은 시드에서는 같은 지뢰 배치가 생성된다")
{
    minesweeper::Game first({6, 6, 7}, 1234);
    minesweeper::Game second({6, 6, 7}, 1234);
    REQUIRE(first.open(0, 0));
    REQUIRE(second.open(0, 0));
    CHECK(first.state().mine_field == second.state().mine_field);
}

TEST_CASE("상태 구조체 할당기를 교체할 수 있다")
{
    minesweeper::Game game({4, 4, 2}, 8, {test_allocate, test_deallocate});
    CHECK(game.state().rows == 4);
    CHECK(game.state().columns == 4);

    minesweeper::Game pool_game({4, 4, 2}, 8, {pool_allocate, nullptr});
    CHECK(pool_game.state().mine_count == 2);
}

TEST_CASE("안전 칸 봇은 지뢰를 밟지 않고 판을 끝낸다")
{
    minesweeper::Game game({6, 6, 6}, 21);
    minesweeper::Bot bot{22};
    for (int turn = 0; turn < 100 && game.state().game_state ==
                                      minesweeper::GameStatus::playing;
         ++turn) {
        CHECK(bot.step(game));
    }
    CHECK(game.state().game_state == minesweeper::GameStatus::won);
}

TEST_CASE("난이도 이름 파싱")
{
    minesweeper::Difficulty difficulty = minesweeper::Difficulty::easy;
    CHECK(minesweeper::parse_difficulty("medium", difficulty));
    CHECK(difficulty == minesweeper::Difficulty::medium);
    CHECK_FALSE(minesweeper::parse_difficulty("expert", difficulty));
    CHECK_FALSE(minesweeper::parse_difficulty(nullptr, difficulty));
}

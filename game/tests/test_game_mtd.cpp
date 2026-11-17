// test_game_mtd.cpp — S3-02 통합: 게임 + 실제 libmtd + game/rules.json
//   1) 정상 플레이(봇, 실시간)에서 alert 0, 감시 등록 실패 0
//   2) 감시 수: 판마다 배치 전 6 · 배치 후 7을 넘지 않고, 해제 후 0
//   3) 외부 변조와 S3-23 승리 일관성 위반은 prediction/behavior JSONL로 남는다
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "minesweeper.hpp"
#include "mtd_glue.hpp"
#include "mtd.hpp"
#include "mtd_test.h"

#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace minesweeper;

namespace {

std::vector<std::string> read_lines(const std::string &path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

}  // namespace

TEST_CASE("S3-02: 정상 플레이 4초(봇, hard, tick 4Hz) → alert 0, 감시 수 6/7 이하, 해제 후 0")
{
    const std::string alert_log = std::string(MTD_TEST_TMP) + "/normal_alerts.jsonl";
    MtdMonitor monitor(MTD_GAME_RULES, alert_log, 10);  // 검사 주기 10ms로 더 자주 본다
    const auto observer = monitor.observer();
    const auto config = config_for(Difficulty::hard);

    std::uint64_t games = 0;
    std::size_t max_before = 0, max_after = 0;
    const auto start = std::chrono::steady_clock::now();
    auto next_tick = start;
    std::uint32_t seed = 100;
    std::unique_ptr<Game> game = std::make_unique<Game>(config, seed, StateAllocator{}, observer);
    Bot bot(seed);
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
        if (std::chrono::steady_clock::now() >= next_tick) {
            game->tick();
            next_tick += std::chrono::milliseconds(250);
        }
        if (game->state().game_state != GameStatus::playing) {
            game.reset();
            CHECK(mtd_test_watch_count() == 0);
            ++games;
            game = std::make_unique<Game>(config, ++seed, StateAllocator{}, observer);
        }
        const std::size_t count = mtd_test_watch_count();
        if (game->state().mines_placed) {
            if (count > max_after) max_after = count;
        } else {
            if (count > max_before) max_before = count;
        }
        bot.step(*game);
        sleep_ms(1);
    }
    game.reset();
    CHECK(mtd_test_watch_count() == 0);
    sleep_ms(50);
    monitor.stop();

    CHECK(games > 0);
    CHECK(monitor.scopes() > 100);
    CHECK(monitor.watch_errors() == 0);
    CHECK(max_before == 6);
    CHECK(max_after == 7);
    CHECK(monitor.alerts() == 0);
    const auto lines = read_lines(alert_log);
    for (const auto &l : lines) MESSAGE("unexpected alert: " << l);
    CHECK(lines.empty());
}

TEST_CASE("S3-02: 구간 밖 timer 변조·지뢰 제거 → behavior alert가 JSONL로 남는다")
{
    const std::string alert_log = std::string(MTD_TEST_TMP) + "/tamper_alerts.jsonl";
    MtdMonitor monitor(MTD_GAME_RULES, alert_log, 10);
    {
        Game game(config_for(Difficulty::easy), 5, StateAllocator{}, monitor.observer());
        REQUIRE(game.open(4, 4));  // 지뢰 배치 → mine_field 감시 시작
        REQUIRE(mtd_test_watch_count() == 7);
        sleep_ms(50);
        REQUIRE(monitor.alerts() == 0);

        GameState &s = game.state();  // 외부 프로세스 쓰기를 흉내: 구간 밖 직접 대입
        s.timer_seconds = 999;
        sleep_ms(100);
        for (auto &cell : s.mine_field) {
            if (cell != 0) {
                cell = 0;
                break;
            }
        }
        sleep_ms(100);
    }
    monitor.stop();

    CHECK(monitor.alerts() == 2);
    const auto lines = read_lines(alert_log);
    REQUIRE(lines.size() == 2);
    CHECK(contains(lines[0], "\"name\":\"timer\""));
    CHECK(contains(lines[0], "\"kind\":\"behavior\""));
    CHECK(contains(lines[0], "\"rule\":\"allowed_in_tags\""));
    CHECK(contains(lines[0], "\"expected\":\"0x0\""));
    CHECK(contains(lines[0], "\"actual\":\"0x3e7\""));
    CHECK(contains(lines[1], "\"name\":\"mine_field\""));
    CHECK(contains(lines[1], "\"rule\":\"immutable\""));
    CHECK(contains(lines[1], "\"expected\":null"));
    CHECK(contains(lines[0], "\"detect_ns\":"));
}

TEST_CASE("S3-23: 정상 승리에서는 일관성 prediction alert가 없다")
{
    const std::string alert_log = std::string(MTD_TEST_TMP) + "/consistent_win_alerts.jsonl";
    MtdMonitor monitor(MTD_GAME_RULES, alert_log, 10);
    {
        Game game(config_for(Difficulty::easy), 17, StateAllocator{}, monitor.observer());
        const auto config = config_for(Difficulty::easy);
        for (std::uint32_t row = 0; row < config.rows &&
                                    game.state().game_state == GameStatus::playing;
             ++row) {
            for (std::uint32_t column = 0; column < config.columns &&
                                           game.state().game_state == GameStatus::playing;
                 ++column) {
                const std::size_t cell = static_cast<std::size_t>(row) * config.columns + column;
                if (game.state().mines_placed && game.state().mine_field[cell] != 0) continue;
                if (game.state().cell_state[cell] == 0) REQUIRE(game.open(row, column));
            }
        }
        CHECK(game.state().game_state == GameStatus::won);
        CHECK(game.state().cells_opened == game.state().safe_cells);
    }
    sleep_ms(50);
    monitor.stop();

    CHECK(monitor.alerts() == 0);
    CHECK(read_lines(alert_log).empty());
}

TEST_CASE("S3-23: 안전 칸을 덜 연 승리 위조는 consistency prediction alert")
{
    const std::string alert_log = std::string(MTD_TEST_TMP) + "/forged_win_alerts.jsonl";
    MtdMonitor monitor(MTD_GAME_RULES, alert_log, 10);
    {
        Game game(config_for(Difficulty::easy), 19, StateAllocator{}, monitor.observer());
        REQUIRE(game.open(0, 0));
        REQUIRE(game.state().cells_opened < game.state().safe_cells);
        {
            mtd::Scope scope{"win-check"};
            game.state().game_state = GameStatus::won;
        }
        sleep_ms(80);
        REQUIRE(monitor.alerts() == 1);
        CHECK(game.state().game_state == GameStatus::won);
    }
    monitor.stop();

    const auto lines = read_lines(alert_log);
    REQUIRE(lines.size() == 1);
    CHECK(contains(lines[0], "\"name\":\"game_state\""));  // 라벨 V-game_state와 같은 키
    CHECK(contains(lines[0], "\"kind\":\"prediction\""));
    CHECK(contains(lines[0], "\"rule\":\"won_requires_all_safe_cells_open\""));
    CHECK(contains(lines[0], "\"expected\":\"0x47\""));
}

#include "minesweeper.hpp"
#include "address_log.hpp"
#include "mtd_glue.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using minesweeper::Difficulty;
using minesweeper::Game;
using minesweeper::GameStatus;

struct Options {
    Difficulty difficulty = Difficulty::easy;
    std::uint32_t seed = std::random_device{}();
    std::uint32_t speed_ms = 25;
    std::uint32_t duration_seconds = 0;
    bool bot = false;
    std::string address_log_path;
    bool use_mtd = true;
    std::string rules_path = MTD_GAME_RULES_DEFAULT;
    std::string alert_log_path;
    std::string stats_path;
};

struct RunStats {
    std::uint64_t won = 0;
    std::uint64_t lost = 0;
    double elapsed_seconds = 0;
};

std::uint32_t parse_uint(const std::string &text, const char *option)
{
    std::size_t parsed = 0;
    const unsigned long value = std::stoul(text, &parsed);
    if (parsed != text.size() || value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string("invalid value for ") + option);
    }
    return static_cast<std::uint32_t>(value);
}

void print_help(const char *program)
{
    std::cout << "Usage: " << program
              << " [--bot] [--difficulty easy|medium|hard] [--seed N]"
                 " [--speed-ms N] [--duration-sec N] [--addr-log FILE]\n"
                 "       [--rules FILE] [--no-mtd] [--alert-log FILE] [--stats FILE]\n"
                 "Interactive keys: w/a/s/d move, o open, f flag, n new game, q quit.\n"
                 "Bot runs without a screen; duration-sec=0 means run until stopped.\n"
                 "libmtd monitoring is on by default (--rules, default "
              << MTD_GAME_RULES_DEFAULT << ").\n"
                 "--alert-log writes alerts as JSONL; --stats writes a JSON run summary\n"
                 "(games, alerts, mtd_end count for the false-positive rate).\n";
}

Options parse_options(int argc, char **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument{argv[i]};
        if (argument == "--help" || argument == "-h") {
            print_help(argv[0]);
            std::exit(0);
        }
        if (argument == "--bot") {
            options.bot = true;
            continue;
        }
        if (argument == "--no-mtd") {
            options.use_mtd = false;
            continue;
        }
        if (argument == "--difficulty" || argument == "--seed" ||
            argument == "--speed-ms" || argument == "--duration-sec" ||
            argument == "--addr-log" || argument == "--rules" ||
            argument == "--alert-log" || argument == "--stats") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value for " + argument);
            }
            const std::string value{argv[++i]};
            if (argument == "--addr-log") {
                options.address_log_path = value;
            } else if (argument == "--rules") {
                options.rules_path = value;
            } else if (argument == "--alert-log") {
                options.alert_log_path = value;
            } else if (argument == "--stats") {
                options.stats_path = value;
            } else if (argument == "--difficulty") {
                if (!minesweeper::parse_difficulty(value.c_str(), options.difficulty)) {
                    throw std::invalid_argument("difficulty must be easy, medium, or hard");
                }
            } else if (argument == "--seed") {
                options.seed = parse_uint(value, argument.c_str());
            } else if (argument == "--speed-ms") {
                options.speed_ms = parse_uint(value, argument.c_str());
            } else {
                options.duration_seconds = parse_uint(value, argument.c_str());
            }
            continue;
        }
        throw std::invalid_argument("unknown option: " + argument);
    }
    if (!options.use_mtd && !options.alert_log_path.empty()) {
        throw std::invalid_argument("--alert-log requires libmtd (remove --no-mtd)");
    }
    return options;
}

const minesweeper::MtdMonitor *g_monitor = nullptr;  // status output only

void print_bot_summary(std::uint64_t completed_games,
                       std::uint64_t lost_games,
                       const Game &game)
{
    std::cout << "games=" << completed_games << " losses=" << lost_games
              << " timer=" << game.state().timer_seconds << "s";
    if (g_monitor != nullptr) {
        std::cout << " alerts=" << g_monitor->alerts() << " scopes=" << g_monitor->scopes();
    }
    std::cout << '\n';
}

int run_bot(const Options &options, minesweeper::StateObserver observer, RunStats &stats)
{
    const auto config = minesweeper::config_for(options.difficulty);
    minesweeper::Bot bot{options.seed ^ 0x9e3779b9u};
    std::uint64_t completed_games = 0;
    std::uint64_t lost_games = 0;
    std::uint32_t game_seed = options.seed;
    auto game = std::make_unique<Game>(config, game_seed,
                                       minesweeper::StateAllocator{}, observer);
    const auto started_at = std::chrono::steady_clock::now();
    auto next_tick = started_at + std::chrono::seconds(1);
    auto next_status = started_at + std::chrono::seconds(10);

    while (options.duration_seconds == 0 ||
           std::chrono::steady_clock::now() - started_at <
               std::chrono::seconds(options.duration_seconds)) {
        const auto now = std::chrono::steady_clock::now();
        while (now >= next_tick) {
            game->tick();
            next_tick += std::chrono::seconds(1);
        }

        if (game->state().game_state != GameStatus::playing) {
            if (game->state().game_state == GameStatus::won) {
                ++completed_games;
            } else {
                ++lost_games;
            }
            ++game_seed;
            game.reset();  // state_free + unwatch before the next state_alloc
            game = std::make_unique<Game>(config, game_seed,
                                          minesweeper::StateAllocator{}, observer);
            continue;
        }

        bot.step(*game);
        if (options.speed_ms != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.speed_ms));
        }

        if (options.duration_seconds != 0 &&
            std::chrono::steady_clock::now() >= next_status) {
            print_bot_summary(completed_games, lost_games, *game);
            next_status += std::chrono::seconds(10);
        }
    }

    print_bot_summary(completed_games, lost_games, *game);
    stats.won = completed_games;
    stats.lost = lost_games;
    stats.elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
    return 0;
}

void draw(const Game &game, std::uint32_t cursor_row, std::uint32_t cursor_column)
{
    const auto &state = game.state();
    std::cout << "\033[2J\033[H"
              << "Minesweeper  time=" << state.timer_seconds
              << "s  mines=" << state.mines_remaining
              << "  opened=" << state.cells_opened;
    if (g_monitor != nullptr) {
        std::cout << "  alerts=" << g_monitor->alerts();
    }
    std::cout << '\n';
    for (std::uint32_t row = 0; row < state.rows; ++row) {
        for (std::uint32_t column = 0; column < state.columns; ++column) {
            const std::size_t cell =
                static_cast<std::size_t>(row) * state.columns + column;
            char symbol = '#';
            if (state.game_state == GameStatus::lost &&
                state.mine_field[cell] != 0) {
                symbol = '*';
            } else if (state.flagged[cell] != 0) {
                symbol = 'F';
            } else if (state.cell_state[cell] != 0) {
                const std::uint32_t adjacent = game.adjacent_mines(row, column);
                symbol = adjacent == 0 ? '.' : static_cast<char>('0' + adjacent);
            }
            if (row == cursor_row && column == cursor_column) {
                std::cout << '[' << symbol << ']';
            } else {
                std::cout << ' ' << symbol << ' ';
            }
        }
        std::cout << '\n';
    }
    std::cout << "w/a/s/d move | o open | f flag | q quit\n> " << std::flush;
}

int run_interactive(const Options &options, minesweeper::StateObserver observer)
{
    const auto config = minesweeper::config_for(options.difficulty);
    std::uint32_t seed = options.seed;
    auto game = std::make_unique<Game>(config, seed,
                                       minesweeper::StateAllocator{}, observer);
    std::uint32_t cursor_row = config.rows / 2;
    std::uint32_t cursor_column = config.columns / 2;
    auto next_tick = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    std::string line;

    while (true) {
        draw(*game, cursor_row, cursor_column);
        if (!std::getline(std::cin, line)) {
            return 0;
        }
        const auto now = std::chrono::steady_clock::now();
        while (now >= next_tick) {
            game->tick();
            next_tick += std::chrono::seconds(1);
        }

        if (line.empty()) {
            continue;
        }
        const char command = line[0];
        if (command == 'q') {
            return 0;
        }
        if (game->state().game_state != GameStatus::playing) {
            if (command == 'n') {
                ++seed;
                game.reset();
                game = std::make_unique<Game>(config, seed,
                                              minesweeper::StateAllocator{}, observer);
                next_tick = std::chrono::steady_clock::now() +
                            std::chrono::seconds(1);
            }
            continue;
        }

        switch (command) {
        case 'w':
            if (cursor_row > 0) {
                --cursor_row;
            }
            break;
        case 's':
            if (cursor_row + 1 < config.rows) {
                ++cursor_row;
            }
            break;
        case 'a':
            if (cursor_column > 0) {
                --cursor_column;
            }
            break;
        case 'd':
            if (cursor_column + 1 < config.columns) {
                ++cursor_column;
            }
            break;
        case 'o':
            game->open(cursor_row, cursor_column);
            break;
        case 'f':
            game->toggle_flag(cursor_row, cursor_column);
            break;
        default:
            break;
        }

        if (game->state().game_state != GameStatus::playing) {
            const bool won = game->state().game_state == GameStatus::won;
            if (!won) {
                draw(*game, cursor_row, cursor_column);
            }
            std::cout << (won ? "\nYou won! " : "\nBoom! ")
                      << "Press n for a new game or q to quit.\n";
            std::string answer;
            if (!std::getline(std::cin, answer) || answer == "q") {
                return 0;
            }
            if (answer == "n") {
                ++seed;
                game.reset();
                game = std::make_unique<Game>(config, seed,
                                              minesweeper::StateAllocator{}, observer);
                next_tick = std::chrono::steady_clock::now() +
                            std::chrono::seconds(1);
            }
        }
    }
}

void write_stats(const Options &options, const RunStats &stats,
                 const minesweeper::MtdMonitor *monitor)
{
    std::ofstream out(options.stats_path);
    if (!out) {
        throw std::runtime_error("cannot open stats file: " + options.stats_path);
    }
    const char *difficulty = options.difficulty == Difficulty::easy     ? "easy"
                             : options.difficulty == Difficulty::medium ? "medium"
                                                                        : "hard";
    out << "{\"mode\":\"" << (options.bot ? "bot" : "interactive") << "\""
        << ",\"difficulty\":\"" << difficulty << "\""
        << ",\"seed\":" << options.seed << ",\"speed_ms\":" << options.speed_ms
        << ",\"duration_sec\":" << options.duration_seconds
        << ",\"elapsed_sec\":" << stats.elapsed_seconds << ",\"games_won\":" << stats.won
        << ",\"games_lost\":" << stats.lost << ",\"mtd\":" << (monitor ? "true" : "false");
    if (monitor != nullptr) {
        out << ",\"alerts\":" << monitor->alerts() << ",\"mtd_end_count\":" << monitor->scopes()
            << ",\"watch_errors\":" << monitor->watch_errors();
    }
    out << "}\n";
    if (!out) {
        throw std::runtime_error("failed to write stats file: " + options.stats_path);
    }
}

}  // namespace

int main(int argc, char **argv)
{
    try {
        const Options options = parse_options(argc, argv);
        std::ofstream address_log;
        std::unique_ptr<minesweeper::AddressLogger> logger;
        minesweeper::StateObserver observer{};
        if (!options.address_log_path.empty()) {
            address_log.open(options.address_log_path);
            if (!address_log) {
                throw std::runtime_error("cannot open address log: " +
                                         options.address_log_path);
            }
            logger = std::make_unique<minesweeper::AddressLogger>(address_log);
            observer = logger->observer();
        }
        std::unique_ptr<minesweeper::MtdMonitor> monitor;
        if (options.use_mtd) {
            monitor = std::make_unique<minesweeper::MtdMonitor>(options.rules_path,
                                                                options.alert_log_path);
            observer = monitor->observer(observer);
            g_monitor = monitor.get();
        }
        RunStats stats;
        const int result = options.bot ? run_bot(options, observer, stats)
                                       : run_interactive(options, observer);
        // run_* owned every Game, so all states are unwatched by now.
        g_monitor = nullptr;
        if (monitor != nullptr) {
            monitor->stop();  // mtd_shutdown: delivers pending alerts, closes the alert log
        }
        if (!options.stats_path.empty()) {
            write_stats(options, stats, monitor.get());
        }
        if (monitor != nullptr && monitor->alert_log_failed()) {
            throw std::runtime_error("failed to write alert log");
        }
        address_log.flush();
        address_log.close();
        if (logger != nullptr && (logger->failed() || !address_log)) {
            throw std::runtime_error("failed to write address log");
        }
        return result;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}

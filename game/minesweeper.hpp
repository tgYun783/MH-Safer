#ifndef MINESWEEPER_HPP
#define MINESWEEPER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

namespace minesweeper {

constexpr std::size_t max_cells = 30u * 16u;

enum class Difficulty {
    easy,
    medium,
    hard
};

enum class GameStatus : std::uint32_t {
    playing = 0,
    won = 1,
    lost = 2
};

struct Config {
    std::uint32_t rows;
    std::uint32_t columns;
    std::uint32_t mines;
};

struct StateAllocator {
    void *(*allocate)(std::size_t);
    // Omit when the allocator owns the memory until its own shutdown (e.g. mtd_alloc).
    void (*deallocate)(void *);
};

struct GameState {
    std::uint32_t timer_seconds = 0;
    std::int32_t mines_remaining = 0;
    std::array<std::uint8_t, max_cells> mine_field{};
    std::array<std::uint8_t, max_cells> cell_state{};
    std::uint32_t cells_opened = 0;
    GameStatus game_state = GameStatus::playing;

    std::uint32_t rows = 0;
    std::uint32_t columns = 0;
    std::uint32_t mine_count = 0;
    bool mines_placed = false;
    std::array<std::uint8_t, max_cells> flagged{};
};

class Game {
public:
    Game(Config config, std::uint32_t seed, StateAllocator allocator = {});
    ~Game();

    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;
    Game(Game &&) = delete;
    Game &operator=(Game &&) = delete;

    GameState &state();
    const GameState &state() const;
    bool open(std::uint32_t row, std::uint32_t column);
    bool toggle_flag(std::uint32_t row, std::uint32_t column);
    void tick();
    std::uint32_t adjacent_mines(std::uint32_t row, std::uint32_t column) const;

private:
    GameState *state_;
    StateAllocator allocator_;
    std::mt19937 random_;

    bool in_bounds(std::uint32_t row, std::uint32_t column) const;
    std::size_t index(std::uint32_t row, std::uint32_t column) const;
    void place_mines(std::size_t safe_index);
    void reveal(std::size_t first_index);
    void update_status();
};

class Bot {
public:
    explicit Bot(std::uint32_t seed);
    bool step(Game &game);

private:
    std::mt19937 random_;
    std::uint32_t actions_ = 0;
};

Config config_for(Difficulty difficulty);
bool parse_difficulty(const char *text, Difficulty &difficulty);

}  // namespace minesweeper

#endif

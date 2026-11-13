#include "minesweeper.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace minesweeper {

namespace {

constexpr std::uint8_t hidden_cell = 0;
constexpr std::uint8_t revealed_cell = 1;
constexpr std::uint8_t mine_cell = 1;
constexpr std::uint8_t flagged_cell = 1;

}  // namespace

Config config_for(Difficulty difficulty)
{
    switch (difficulty) {
    case Difficulty::easy:
        return {9, 9, 10};
    case Difficulty::medium:
        return {16, 16, 40};
    case Difficulty::hard:
        return {16, 30, 99};
    }
    throw std::invalid_argument("unknown difficulty");
}

bool parse_difficulty(const char *text, Difficulty &difficulty)
{
    if (text == nullptr) {
        return false;
    }
    const std::string value{text};
    if (value == "easy") {
        difficulty = Difficulty::easy;
    } else if (value == "medium") {
        difficulty = Difficulty::medium;
    } else if (value == "hard") {
        difficulty = Difficulty::hard;
    } else {
        return false;
    }
    return true;
}

Game::Game(Config config,
           std::uint32_t seed,
           StateAllocator allocator,
           StateObserver observer)
    : state_(nullptr), allocator_(allocator), observer_(observer), random_(seed)
{
    const std::uint64_t total_cells =
        static_cast<std::uint64_t>(config.rows) * config.columns;
    if (config.rows == 0 || config.columns == 0 || total_cells > max_cells ||
        config.mines >= total_cells) {
        throw std::invalid_argument("invalid board dimensions or mine count");
    }
    if (allocator_.allocate == nullptr && allocator_.deallocate != nullptr) {
        throw std::invalid_argument("deallocator requires a custom allocator");
    }

    void *memory = allocator_.allocate != nullptr
                       ? allocator_.allocate(sizeof(GameState))
                       : ::operator new(sizeof(GameState), std::nothrow);
    if (memory == nullptr) {
        throw std::bad_alloc{};
    }

    try {
        state_ = new (memory) GameState{};
        state_->rows = config.rows;
        state_->columns = config.columns;
        state_->mine_count = config.mines;
        state_->mines_remaining = static_cast<std::int32_t>(config.mines);
        if (observer_.state_alloc != nullptr) {
            observer_.state_alloc(observer_.user, state_);
        }
    } catch (...) {
        if (allocator_.deallocate != nullptr) {
            allocator_.deallocate(memory);
        } else if (allocator_.allocate == nullptr) {
            ::operator delete(memory);
        }
        throw;
    }
}

Game::~Game()
{
    if (state_ != nullptr) {
        if (observer_.state_free != nullptr) {
            observer_.state_free(observer_.user, state_);
        }
        state_->~GameState();
        if (allocator_.deallocate != nullptr) {
            allocator_.deallocate(state_);
        } else if (allocator_.allocate == nullptr) {
            ::operator delete(state_);
        }
    }
}

GameState &Game::state()
{
    return *state_;
}

const GameState &Game::state() const
{
    return *state_;
}

bool Game::in_bounds(std::uint32_t row, std::uint32_t column) const
{
    return row < state_->rows && column < state_->columns;
}

std::size_t Game::index(std::uint32_t row, std::uint32_t column) const
{
    return static_cast<std::size_t>(row) * state_->columns + column;
}

void Game::place_mines(std::size_t safe_index)
{
    std::vector<std::size_t> candidates;
    candidates.reserve(static_cast<std::size_t>(state_->rows) * state_->columns - 1);
    const std::size_t total_cells =
        static_cast<std::size_t>(state_->rows) * state_->columns;
    for (std::size_t cell = 0; cell < total_cells; ++cell) {
        if (cell != safe_index) {
            candidates.push_back(cell);
        }
    }
    std::shuffle(candidates.begin(), candidates.end(), random_);
    for (std::uint32_t mine = 0; mine < state_->mine_count; ++mine) {
        state_->mine_field[candidates[mine]] = mine_cell;
    }
    state_->mines_placed = true;
    if (observer_.field_watch != nullptr) {
        observer_.field_watch(observer_.user, state_);
    }
}

std::uint32_t Game::adjacent_mines(std::uint32_t row, std::uint32_t column) const
{
    if (!in_bounds(row, column) || !state_->mines_placed) {
        return 0;
    }

    std::uint32_t count = 0;
    const std::uint32_t row_start = row == 0 ? 0 : row - 1;
    const std::uint32_t column_start = column == 0 ? 0 : column - 1;
    const std::uint32_t row_end = std::min(row + 1, state_->rows - 1);
    const std::uint32_t column_end = std::min(column + 1, state_->columns - 1);
    for (std::uint32_t neighbor_row = row_start; neighbor_row <= row_end; ++neighbor_row) {
        for (std::uint32_t neighbor_column = column_start;
             neighbor_column <= column_end;
             ++neighbor_column) {
            if (neighbor_row != row || neighbor_column != column) {
                count += state_->mine_field[index(neighbor_row, neighbor_column)];
            }
        }
    }
    return count;
}

void Game::reveal(std::size_t first_index)
{
    std::array<std::size_t, max_cells> queue{};
    std::size_t head = 0;
    std::size_t tail = 0;

    state_->cell_state[first_index] = revealed_cell;
    ++state_->cells_opened;
    const std::uint32_t first_row =
        static_cast<std::uint32_t>(first_index / state_->columns);
    const std::uint32_t first_column =
        static_cast<std::uint32_t>(first_index % state_->columns);
    if (adjacent_mines(first_row, first_column) == 0) {
        queue[tail++] = first_index;
    }

    while (head < tail) {
        const std::size_t current = queue[head++];
        const std::uint32_t row =
            static_cast<std::uint32_t>(current / state_->columns);
        const std::uint32_t column =
            static_cast<std::uint32_t>(current % state_->columns);
        const std::uint32_t row_start = row == 0 ? 0 : row - 1;
        const std::uint32_t column_start = column == 0 ? 0 : column - 1;
        const std::uint32_t row_end = std::min(row + 1, state_->rows - 1);
        const std::uint32_t column_end = std::min(column + 1, state_->columns - 1);

        for (std::uint32_t neighbor_row = row_start;
             neighbor_row <= row_end;
             ++neighbor_row) {
            for (std::uint32_t neighbor_column = column_start;
                 neighbor_column <= column_end;
                 ++neighbor_column) {
                const std::size_t neighbor = index(neighbor_row, neighbor_column);
                if (state_->cell_state[neighbor] != hidden_cell ||
                    state_->flagged[neighbor] != 0 ||
                    state_->mine_field[neighbor] == mine_cell) {
                    continue;
                }

                state_->cell_state[neighbor] = revealed_cell;
                ++state_->cells_opened;
                if (adjacent_mines(neighbor_row, neighbor_column) == 0) {
                    queue[tail++] = neighbor;
                }
            }
        }
    }
}

void Game::update_status(bool mine_hit)
{
    if (mine_hit) {
        state_->game_state = GameStatus::lost;
        return;
    }
    const std::uint32_t safe_cells =
        state_->rows * state_->columns - state_->mine_count;
    if (state_->cells_opened == safe_cells) {
        state_->game_state = GameStatus::won;
    }
}

bool Game::open(std::uint32_t row, std::uint32_t column)
{
    if (!in_bounds(row, column) || state_->game_state != GameStatus::playing) {
        return false;
    }
    const std::size_t cell = index(row, column);
    if (state_->cell_state[cell] != hidden_cell || state_->flagged[cell] != 0) {
        return false;
    }
    if (!state_->mines_placed) {
        place_mines(cell);
    }
    if (state_->mine_field[cell] == mine_cell) {
        update_status(true);
        return true;
    }

    reveal(cell);
    update_status(false);
    return true;
}

bool Game::toggle_flag(std::uint32_t row, std::uint32_t column)
{
    if (!in_bounds(row, column) || state_->game_state != GameStatus::playing) {
        return false;
    }
    const std::size_t cell = index(row, column);
    if (state_->cell_state[cell] != hidden_cell) {
        return false;
    }

    if (state_->flagged[cell] == flagged_cell) {
        state_->flagged[cell] = 0;
        ++state_->mines_remaining;
    } else {
        state_->flagged[cell] = flagged_cell;
        --state_->mines_remaining;
    }
    return true;
}

void Game::tick()
{
    if (state_->mines_placed && state_->game_state == GameStatus::playing &&
        state_->timer_seconds < std::numeric_limits<std::uint32_t>::max()) {
        ++state_->timer_seconds;
    }
}

Bot::Bot(std::uint32_t seed) : random_(seed) {}

bool Bot::step(Game &game)
{
    const GameState &state = game.state();
    if (state.game_state != GameStatus::playing) {
        return false;
    }

    const std::size_t total_cells =
        static_cast<std::size_t>(state.rows) * state.columns;
    if (state.mines_placed && actions_ % 7 == 0) {
        std::vector<std::size_t> unflagged_mines;
        for (std::size_t cell = 0; cell < total_cells; ++cell) {
            if (state.mine_field[cell] == mine_cell && state.flagged[cell] == 0) {
                unflagged_mines.push_back(cell);
            }
        }
        if (!unflagged_mines.empty()) {
            std::uniform_int_distribution<std::size_t> choose(
                0, unflagged_mines.size() - 1);
            const std::size_t cell = unflagged_mines[choose(random_)];
            game.toggle_flag(static_cast<std::uint32_t>(cell / state.columns),
                             static_cast<std::uint32_t>(cell % state.columns));
        }
    }

    std::vector<std::size_t> safe_hidden_cells;
    for (std::size_t cell = 0; cell < total_cells; ++cell) {
        if (state.cell_state[cell] == hidden_cell && state.flagged[cell] == 0 &&
            (!state.mines_placed || state.mine_field[cell] != mine_cell)) {
            safe_hidden_cells.push_back(cell);
        }
    }
    if (safe_hidden_cells.empty()) {
        return false;
    }

    std::uniform_int_distribution<std::size_t> choose(0, safe_hidden_cells.size() - 1);
    const std::size_t cell = safe_hidden_cells[choose(random_)];
    ++actions_;
    return game.open(static_cast<std::uint32_t>(cell / state.columns),
                     static_cast<std::uint32_t>(cell % state.columns));
}

}  // namespace minesweeper
